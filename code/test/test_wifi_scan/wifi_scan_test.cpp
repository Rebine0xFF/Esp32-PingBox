#include <Arduino.h>
#include <WiFi.h>

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("\nWiFi configuration complete. Ready to scan.");
}

void loop() {
  Serial.println("Starting WiFi scan...");

  int n = WiFi.scanNetworks();

  Serial.println("Scan complete.");

  if (n == 0) {
    Serial.println("No networks found.");
  } else {
    Serial.print(n);
    Serial.println(" networks found:");

    for (int i = 0; i < n; ++i) {
      Serial.print(i + 1);
      Serial.print(": ");

      Serial.print(WiFi.SSID(i));

      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(" dBm)");

      Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " [Open]" : " *");

      delay(10);
    }
  }
  Serial.println("");

  delay(5000);
}
