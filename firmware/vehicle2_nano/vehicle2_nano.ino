#include <SPI.h>
#include <RF24.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// NRF24L01 pins
#define CE_PIN  9
#define CSN_PIN 10

RF24 radio(CE_PIN, CSN_PIN);
Adafruit_MPU6050 mpu;

// Must match Mega address
const byte address[6] = "CAR01";

// MUST MATCH THE MEGA STRUCTURE EXACTLY
struct DataPacket {
  uint8_t vehicleID;
  uint16_t counter;
  float speed;

  uint8_t mpuPresent;

  int16_t ax_mg;
  int16_t ay_mg;
  int16_t az_mg;

  int16_t gx_dps10;
  int16_t gy_dps10;
  int16_t gz_dps10;
};

DataPacket receivedData;
DataPacket responseData;

bool mpuOK = false;
uint16_t nanoCounter = 0;

// --------------------------------
// READ MPU USING ADAFRUIT LIBRARY
// --------------------------------
bool readNanoMPU() {
  sensors_event_t a, g, temp;

  if (!mpuOK) {
    return false;
  }

  mpu.getEvent(&a, &g, &temp);

  // Convert acceleration from m/s^2 to milli-g
  responseData.ax_mg = a.acceleration.x * 1000.0 / 9.80665;
  responseData.ay_mg = a.acceleration.y * 1000.0 / 9.80665;
  responseData.az_mg = a.acceleration.z * 1000.0 / 9.80665;

  // Convert angular velocity from rad/s to 0.1 degrees/s
  responseData.gx_dps10 = g.gyro.x * 1800.0 / PI;
  responseData.gy_dps10 = g.gyro.y * 1800.0 / PI;
  responseData.gz_dps10 = g.gyro.z * 1800.0 / PI;

  return true;
}

// --------------------------------
// SETUP
// --------------------------------
void setup() {
  Serial.begin(9600);
  Wire.begin();

  Serial.println("NANO STARTING...");

  // MPU failure must not stop radio communication
  mpuOK = mpu.begin(0x68);

  if (mpuOK) {
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

    Serial.println("MPU INITIALIZATION SUCCESS");
  } else {
    Serial.println("MPU NOT AVAILABLE");
    Serial.println("Continuing without MPU data");
  }

  if (!radio.begin()) {
    Serial.println("NRF24L01 not detected!");
    while (1) {}
  }

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(100);
  radio.setRetries(5, 15);

  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);

  radio.startListening();

  Serial.println("NANO TWO-WAY COMMUNICATION READY");
  Serial.println("Waiting for Mega data...");
}

// --------------------------------
// MAIN LOOP
// --------------------------------
void loop() {
  if (radio.available()) {

    // Receive Mega packet
    radio.read(&receivedData, sizeof(receivedData));

    Serial.println("\n--- MEGA DATA RECEIVED ---");

    Serial.print("Vehicle ID: ");
    Serial.println(receivedData.vehicleID);

    Serial.print("Counter: ");
    Serial.println(receivedData.counter);

    Serial.print("Speed: ");
    Serial.println(receivedData.speed);

    Serial.print("Mega MPU: ");
    Serial.println(
      receivedData.mpuPresent ? "OK" : "NOT AVAILABLE"
    );

    if (receivedData.mpuPresent) {
      Serial.print("Mega Accel mg: ");
      Serial.print(receivedData.ax_mg);
      Serial.print(", ");
      Serial.print(receivedData.ay_mg);
      Serial.print(", ");
      Serial.println(receivedData.az_mg);

      Serial.print("Mega Gyro 0.1 dps: ");
      Serial.print(receivedData.gx_dps10);
      Serial.print(", ");
      Serial.print(receivedData.gy_dps10);
      Serial.print(", ");
      Serial.println(receivedData.gz_dps10);
    }

    // Prepare Nano's own response
    responseData.vehicleID = 2;
    responseData.counter = ++nanoCounter;

    // Preserve the original example-speed concept
    responseData.speed = 25.5 + (nanoCounter % 10);

    responseData.mpuPresent = mpuOK ? 1 : 0;

    if (mpuOK && !readNanoMPU()) {
      mpuOK = false;
      responseData.mpuPresent = 0;
      Serial.println("MPU read failed; marking unavailable");
    }

    if (!responseData.mpuPresent) {
      responseData.ax_mg = 0;
      responseData.ay_mg = 0;
      responseData.az_mg = 0;

      responseData.gx_dps10 = 0;
      responseData.gy_dps10 = 0;
      responseData.gz_dps10 = 0;
    }

    // Send Nano data back to Mega
    radio.stopListening();

    bool success = radio.write(
      &responseData, sizeof(responseData)
    );

    if (success) {
      Serial.println("\n--- NANO DATA SENT ---");

      Serial.print("Vehicle ID: ");
      Serial.println(responseData.vehicleID);

      Serial.print("Counter: ");
      Serial.println(responseData.counter);

      Serial.print("Speed: ");
      Serial.println(responseData.speed);

      Serial.print("Nano MPU: ");
      Serial.println(
        responseData.mpuPresent ? "OK" : "NOT AVAILABLE"
      );

      if (responseData.mpuPresent) {
        Serial.print("Accel mg: ");
        Serial.print(responseData.ax_mg);
        Serial.print(", ");
        Serial.print(responseData.ay_mg);
        Serial.print(", ");
        Serial.println(responseData.az_mg);

        Serial.print("Gyro 0.1 dps: ");
        Serial.print(responseData.gx_dps10);
        Serial.print(", ");
        Serial.print(responseData.gy_dps10);
        Serial.print(", ");
        Serial.println(responseData.gz_dps10);
      }

      Serial.println("-----------------------");

    } else {
      Serial.println("Nano transmission failed!");
    }

    // Return to receive mode
    radio.startListening();
  }
}