#define BLYNK_TEMPLATE_ID "TMPL61cXXX1oC"
#define BLYNK_TEMPLATE_NAME "RFID rezervasyon sistemi"

#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <BlynkSimpleEsp8266.h>
#include <SPI.h>
#include <MFRC522.h> // RFID için gerekli kütüphane

// Wi-Fi ve Firebase ayarları
#define WIFI_SSID "Galaxy A54 5G DAE2"
#define WIFI_PASSWORD "5315916511"
#define BLYNK_AUTH_TOKEN "sp5WyRQoa-RJkh9_7Uzwc3KCVUwoCFa3" // Blynk'den aldığınız token

FirebaseData firebaseData;
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;

// RFID ayarları
#define RST_PIN D3 // RFID reset pini
#define SS_PIN D4  // RFID SS pini
MFRC522 rfid(SS_PIN, RST_PIN);

// Blynk LCD Widget
WidgetLCD lcd(V1); // LCD Widget (V1 kullanılacak)

// Geçerli saati döndüren fonksiyon
String getCurrentTime() {
  // Şu anda sabit bir saat döndürmek için örnek bir saat kullanılıyor
  return "14:10"; // Örneğin: geçerli zaman
}

// Başlangıç saati ve süresine göre geçerli zaman kontrolü
bool isWithinAllowedTime(String startTime, int duration) {
  int startHour = startTime.substring(0, 2).toInt();
  int startMinute = startTime.substring(3, 5).toInt();

  int currentHour = getCurrentTime().substring(0, 2).toInt();
  int currentMinute = getCurrentTime().substring(3, 5).toInt();

  // Başlangıç ve bitiş zamanlarını dakika olarak hesaplayın
  int startInMinutes = startHour * 60 + startMinute;
  int endInMinutes = startInMinutes + duration;

  int currentInMinutes = currentHour * 60 + currentMinute;

  // Geçerli zaman aralıkta mı kontrol edin
  return currentInMinutes >= startInMinutes && currentInMinutes <= endInMinutes;
}

void setup() {
  Serial.begin(9600);

  // Wi-Fi bağlantısı
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi bağlı!");

  // Firebase yapılandırma
  firebaseConfig.host = "rfid-project-c26c6-default-rtdb.firebaseio.com";
  firebaseConfig.signer.tokens.legacy_token = "Sizin_FIREBASE_TOKEN'iniz";

  // Firebase başlatma
  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true);

  // Blynk başlatma
  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Firebase'e ve Blynk'e bağlanıldı!");

  // RFID başlatma
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID başlatıldı!");
}

void loop() {
  Blynk.run();

  // RFID kartı tarama
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return; // Eğer yeni bir kart okunmamışsa döngüyü atla
  }

  // Okutulan UID'yi al
  String scannedUID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    scannedUID += String(rfid.uid.uidByte[i], HEX); // UID'yi hex formatında birleştir
  }
  scannedUID.toUpperCase(); // UID'yi büyük harfe çevir
  scannedUID.trim();        // Ekstra boşlukları kaldır

  Serial.println("Okutulan UID: " + scannedUID);

  // Firebase'deki tüm kullanıcıları kontrol et
  if (Firebase.getJSON(firebaseData, "/users")) {
    FirebaseJson json = firebaseData.jsonObject();
    size_t len = json.iteratorBegin();
    String key, value;
    int type;

    bool userFound = false;

    for (size_t i = 0; i < len; i++) {
      json.iteratorGet(i, type, key, value);

      FirebaseJson userJson;
      userJson.setJsonData(value);

      FirebaseJsonData jsonData;

      if (userJson.get(jsonData, "cardUID")) {
        String userUID = jsonData.stringValue;
        userUID.trim();
        userUID.toUpperCase();

        if (userUID.equals(scannedUID)) {
          // Kullanıcı bulundu
          userJson.get(jsonData, "name");
          String name = jsonData.stringValue;

          userJson.get(jsonData, "startTime");
          String startTime = jsonData.stringValue;

          userJson.get(jsonData, "duration");
          int duration = jsonData.intValue;

          userJson.get(jsonData, "entryCount");
          int entryCount = jsonData.intValue;

          // Zaman kontrolü
          String status = isWithinAllowedTime(startTime, duration) ? "active" : "passive";
          userJson.set("status", status);
          Firebase.setJSON(firebaseData, "/users/" + key, userJson);

          // Giriş sayısını yalnızca active durumunda artır
          if (status == "active") {
            entryCount++;
            userJson.set("entryCount", entryCount);
            Firebase.setJSON(firebaseData, "/users/" + key, userJson);
            Serial.println("Giriş kabul edildi. Yeni entryCount: " + String(entryCount));
          } else {
            Serial.println("Giriş reddedildi. Kullanıcı pasif. EntryCount değiştirilmedi.");
          }

          // Blynk'e gönder
          Blynk.virtualWrite(V2, name);          // Kullanıcı adı
          Blynk.virtualWrite(V3, status);       // Durum (active/passive)
          Blynk.virtualWrite(V4, entryCount);   // Giriş sayısı

          // V5 pini için kontrol
          if (entryCount > 13) {
            Blynk.virtualWrite(V5, "Verimsiz");
          } else {
            Blynk.virtualWrite(V5, "Verimli");
          }

          // LCD'ye yazdır
          lcd.clear();
          lcd.print(0, 0, "Durum: " + status);
          lcd.print(0, 1, "K: " + status);

          Serial.println("Blynk'e veri gönderildi!");

          userFound = true;
          break; // Doğru kullanıcıyı bulduktan sonra döngüyü durdur
        }
      }
    }

    json.iteratorEnd();

    if (!userFound) {
      Serial.println("Kart UID'si hiçbir kullanıcı ile eşleşmedi!");
      Blynk.virtualWrite(V2, "Tanımsız Kullanıcı");
      Blynk.virtualWrite(V3, "passive");
      Blynk.virtualWrite(V4, 0);
      Blynk.virtualWrite(V5, ""); // Tanımsız kullanıcıda da V5 pinini temizle
    }
  } else {
    Serial.println("Firebase'den veri alınamadı! Hata: " + firebaseData.errorReason());
  }

  delay(500);
}
