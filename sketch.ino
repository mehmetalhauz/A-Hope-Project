#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// Pin Tanımlamaları
#define PIN_BTN      2
#define PIN_RGB_B    3
#define PIN_RGB_G    5
#define PIN_RGB_R    6
#define PIN_BUZZER   8

// Ayarlar
#define SHOCK_THRESHOLD    5.5   // Ani ivme değişimi (Delta/Şok) eşiği
#define COUNTDOWN_SECONDS  10    // Alarm öncesi tanınan süre (sn)

// Donanım Nesneleri
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_MPU6050 mpu;

enum SystemState {
  STATE_NORMAL,
  STATE_WARNING,
  STATE_EMERGENCY
};

SystemState currentState = STATE_NORMAL;
unsigned long warningStartTime = 0;
unsigned long lastBuzzerToggle = 0;
unsigned long immunityEndTime = 0; // İptal sonrası koruma süresi
bool buzzerState = false;

float baselineAccel = 9.8; // Referans ivme değeri (Otomatik sıfırlanır)

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_RGB_G, OUTPUT);
  pinMode(PIN_RGB_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // LCD Başlatma
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sistem Basliyor");
  setRGB(0, 0, 255); // Mavi (Açılış)
  delay(1000);

  // MPU6050 Başlatma
  if (!mpu.begin()) {
    lcd.clear();
    lcd.print("MPU6050 Hatasi!");
    while (1);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  // İlk referans noktasını al ve sistemi başlat
  calibrateBaseline();
  setNormalState();
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Anlık vektörel ivme
  float currentAccel = sqrt(sq(a.acceleration.x) + sq(a.acceleration.y) + sq(a.acceleration.z));
  
  // Ani Değişim (Darbe / Şok) Miktarı
  float shock = abs(currentAccel - baselineAccel);

  // --- 1. NORMAL DURUM ---
  if (currentState == STATE_NORMAL) {
    // Eğer koruma süresi bittiyse ve ani bir şok/darbe olduysa
    if (millis() > immunityEndTime && shock > SHOCK_THRESHOLD) {
      Serial.print("Darbe Algilandi! Ani Degisim (Delta): ");
      Serial.println(shock);
      triggerWarningState();
    }
  }

  // --- 2. UYARI / İPTAL SÜRECİ ---
  else if (currentState == STATE_WARNING) {
    unsigned long elapsedMillis = millis() - warningStartTime;
    int remainingSeconds = COUNTDOWN_SECONDS - (elapsedMillis / 1000);

    // Geri sayımı LCD'de göster
    lcd.setCursor(11, 1);
    if (remainingSeconds < 10) lcd.print("0");
    lcd.print(remainingSeconds);
    lcd.print("s");

    // Kesik kesik uyarı sesi ve sarı flaş
    if (millis() - lastBuzzerToggle >= 400) {
      lastBuzzerToggle = millis();
      buzzerState = !buzzerState;
      if (buzzerState) {
        tone(PIN_BUZZER, 1000);
        setRGB(255, 100, 0); // Sarı
      } else {
        noTone(PIN_BUZZER);
        setRGB(0, 0, 0);
      }
    }

    // Butona basılırsa İPTAL ET VE SIFIRLA
    if (digitalRead(PIN_BTN) == LOW) {
      noTone(PIN_BUZZER);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ALARM IPTAL");
      lcd.setCursor(0, 1);
      lcd.print("SIFIRLANIYOR...");
      setRGB(0, 255, 0); // Yeşil
      
      // Çubuğun o an durduğu yeri yeni "0 / Normal" noktası yap
      calibrateBaseline();
      
      delay(1500);
      setNormalState();
    }

    // Süre dolduysa ACİL DURUMA geç
    if (remainingSeconds <= 0) {
      triggerEmergencyState();
    }
  }

  // --- 3. ACİL DURUM ---
  else if (currentState == STATE_EMERGENCY) {
    tone(PIN_BUZZER, (millis() / 300 % 2 == 0) ? 800 : 1200);

    // Kırmızı flaşör
    if (millis() / 250 % 2 == 0) {
      setRGB(255, 0, 0);
    } else {
      setRGB(0, 0, 0);
    }

    // Sistemi sıfırlamak için butona basılabilir
    if (digitalRead(PIN_BTN) == LOW) {
      noTone(PIN_BUZZER);
      calibrateBaseline();
      setNormalState();
      delay(500);
    }
  }
}

// Sensörün o anki halini "Normal Durum" kabul etme fonksiyonu
void calibrateBaseline() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  baselineAccel = sqrt(sq(a.acceleration.x) + sq(a.acceleration.y) + sq(a.acceleration.z));
  immunityEndTime = millis() + 2000; // İptalden sonraki 2 saniye sistemi tetikleme
}

void setNormalState() {
  currentState = STATE_NORMAL;
  noTone(PIN_BUZZER);
  setRGB(0, 255, 0); // Yeşil (Güvende)
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SISTEM: AKTIF");
  lcd.setCursor(0, 1);
  lcd.print("DURUM: GUVENDE");
}

void triggerWarningState() {
  currentState = STATE_WARNING;
  warningStartTime = millis();
  lastBuzzerToggle = millis();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DUSME ALGILANDI!");
  lcd.setCursor(0, 1);
  lcd.print("Iptal Sure:");
}

void triggerEmergencyState() {
  currentState = STATE_EMERGENCY;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("! ACIL DURUM !");
  lcd.setCursor(0, 1);
  lcd.print("SMS GONDERILDI!");

  Serial.println("\n======================================");
  Serial.println("[GSM MODUL]: ACIL DURUM BILDIRIMI!");
  Serial.println("Kullanici dustu ve teyit vermedi!");
  Serial.println("Konum: https://maps.google.com/?q=41.7989,20.9083");
  Serial.println("Acil Servis ve Gostivar Yakinlari Araniyor...");
  Serial.println("======================================\n");
}

void setRGB(int red, int green, int blue) {
  analogWrite(PIN_RGB_R, red);
  analogWrite(PIN_RGB_G, green);
  analogWrite(PIN_RGB_B, blue);
}