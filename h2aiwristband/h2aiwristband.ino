#include <Wire.h>
#include <MPU6050_tockn.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "DHT.h"
#include <LiquidCrystal_I2C.h>  // Include the I2C LCD library

// Replace with your WiFi credentials
const char* ssid = "";
const char* password = "";

// Firebase configuration
#define FIREBASE_HOST "" // Replace with your Firebase host
#define API_KEY ""       // Replace with your Firebase API key

// DHT11 configuration
#define DHTPIN 4     // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11   // DHT 11

// Initialize DHT sensor
DHT dht(DHTPIN, DHTTYPE);

// Define Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// MPU6050 object
MPU6050 mpu6050(Wire);

// Tremor detection variables
unsigned long tremorStartTime = 0;  // Variable to store the start time of the tremor
unsigned long tremorDuration = 0;   // Variable to store the total duration of the tremor
bool tremorDetected = false;        // Flag to indicate if a tremor is currently detected

// I2C LCD configuration
#define LCD_ADDRESS 0x27  // I2C address of the LCD (change if necessary)
#define LCD_COLUMNS 16    // 16 columns
#define LCD_ROWS 2        // 2 rows
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);  // Initialize the LCD

// Button configuration
#define BUTTON_PIN 5  // Define the pin where the button is connected
int messageCycle = 0; // 0 = No message, 1 = First message, 2 = Second message

// Button debouncing
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;  // Debounce delay in milliseconds

bool debounceButton(int pin) {
  bool currentState = digitalRead(pin);
  if (currentState == HIGH) {  // Button is pressed (assuming active LOW)
    if ((millis() - lastDebounceTime) > debounceDelay) {
      lastDebounceTime = millis();
      return true;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);  // Start serial communication

  // Initialize DHT sensor
  dht.begin();

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  // Configure Firebase
  config.api_key = API_KEY;
  config.database_url = FIREBASE_HOST;

  // Authenticate with Firebase
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase authentication successful");
  } else {
    Serial.printf("Firebase authentication failed: %s\n", config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Initialize MPU6050
  Wire.begin();          // Initialize I2C communication
  mpu6050.begin();       // Initialize MPU6050
  mpu6050.calcGyroOffsets(true);  // Calibrate gyroscope

  // Initialize button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Initialize I2C LCD
  lcd.init();                      // Initialize the LCD
  lcd.backlight();                 // Turn on the backlight
  lcd.setCursor(0, 0);             // Set cursor to the first column, first row
  lcd.print("System Ready");       // Display initial message
}

void loop() {
  // Check if the button is pressed
  if (debounceButton(BUTTON_PIN)) {
    messageCycle = (messageCycle + 1) % 3;  // Cycle through 0, 1, 2
    lcd.clear();                            // Clear the display
    lcd.setCursor(0, 0);                   // Set cursor to the first column, first row

    switch (messageCycle) {
      case 0:
        lcd.print("No Message");           // Display "No Message"
        break;
      case 1:
        lcd.print("Serial 7");
        lcd.setCursor(0, 1);   
        lcd.print("Subtraction - 50");            // Display "Message 1"
        break;
      case 2:
        lcd.print("Draw a clock");   
        lcd.setCursor(0, 1);   
        lcd.print("showing 9:00");           // Display "Message 2"
        break;
    }
    delay(500);  // Add a small delay to avoid multiple toggles
  }

  // Tremor detection logic
  const int numSamples = 50;  // Number of samples for variance calculation
  float ax[numSamples];       // Array to store accelerometer data
  float variance = 0;         // Variable to store variance

  // Collect accelerometer data
  for (int i = 0; i < numSamples; i++) {
    mpu6050.update();         // Update MPU6050 data
    ax[i] = mpu6050.getAccX();  // Store X-axis accelerometer data
    delay(20);                // 50 Hz sampling rate (20 ms delay)
  }

  // Calculate mean
  float mean = 0;
  for (int i = 0; i < numSamples; i++) {
    mean += ax[i];
  }
  mean /= numSamples;

  // Calculate variance
  for (int i = 0; i < numSamples; i++) {
    variance += pow(ax[i] - mean, 2);
  }
  variance /= numSamples;

  // Check if variance exceeds 0.05
  if (variance >= 0.05) {
    if (!tremorDetected) {
      tremorDetected = true;
      tremorStartTime = millis();  // Record the start time of the tremor
    }
  } else {
    if (tremorDetected) {
      tremorDetected = false;
      tremorDuration += millis() - tremorStartTime;  // Update the total tremor duration
    }
  }

  // Print the total tremor duration if no tremor is detected and duration was recorded
  if (!tremorDetected && tremorDuration > 0) {
    int tremorDurationSeconds = tremorDuration / 1000; // Convert milliseconds to seconds
    Serial.println("Tremor detected for " + String(tremorDurationSeconds) + " seconds");

    // Send tremor duration (as integer) to Firebase
    if (Firebase.RTDB.setInt(&fbdo, "/tremor/duration", tremorDurationSeconds)) {
      Serial.println("Tremor data sent to Firebase successfully");
    } else {
      Serial.printf("Failed to send tremor data to Firebase: %s\n", fbdo.errorReason().c_str());
    }

    tremorDuration = 0;  // Reset the tremor duration
  }

  // DHT11 sensor logic
  // Wait a few seconds between measurements.
  delay(2000);

  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  float f = dht.readTemperature(true);

  // Check if any reads failed and exit early (to try again).
  if (isnan(h) || isnan(t) || isnan(f)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  // Compute heat index in Fahrenheit (the default)
  float hif = dht.computeHeatIndex(f, h);
  // Compute heat index in Celsius (isFahreheit = false)
  float hic = dht.computeHeatIndex(t, h, false);

  // Print sensor data to Serial Monitor
  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.print(F("°C "));
  Serial.print(f);
  Serial.print(F("°F  Heat index: "));
  Serial.print(hic);
  Serial.print(F("°C "));
  Serial.print(hif);
  Serial.println(F("°F"));

  // Send sensor data to Firebase
  sendDataToFirebase(h, t, f, hic, hif);

  delay(2000); // Wait for 2 seconds before sending the next data
}

void sendDataToFirebase(float humidity, float tempC, float tempF, float heatIndexC, float heatIndexF) {
  if (Firebase.RTDB.setFloat(&fbdo, "/sensor/humidity", humidity)) {
    Serial.println("Humidity data sent to Firebase successfully");
  } else {
    Serial.printf("Failed to send humidity data to Firebase: %s\n", fbdo.errorReason().c_str());
  }

  if (Firebase.RTDB.setFloat(&fbdo, "/sensor/temperatureC", tempC)) {
    Serial.println("Temperature (C) data sent to Firebase successfully");
  } else {
    Serial.printf("Failed to send temperature (C) data to Firebase: %s\n", fbdo.errorReason().c_str());
  }

  if (Firebase.RTDB.setFloat(&fbdo, "/sensor/temperatureF", tempF)) {
    Serial.println("Temperature (F) data sent to Firebase successfully");
  } else {
    Serial.printf("Failed to send temperature (F) data to Firebase: %s\n", fbdo.errorReason().c_str());
  }

  if (Firebase.RTDB.setFloat(&fbdo, "/sensor/heatIndexC", heatIndexC)) {
    Serial.println("Heat Index (C) data sent to Firebase successfully");
  } else {
    Serial.printf("Failed to send heat index (C) data to Firebase: %s\n", fbdo.errorReason().c_str());
  }

  if (Firebase.RTDB.setFloat(&fbdo, "/sensor/heatIndexF", heatIndexF)) {
    Serial.println("Heat Index (F) data sent to Firebase successfully");
  } else {
    Serial.printf("Failed to send heat index (F) data to Firebase: %s\n", fbdo.errorReason().c_str());
  }
}
