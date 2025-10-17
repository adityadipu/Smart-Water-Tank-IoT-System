IoT Smart Water Tank Monitoring & Control System

This repository contains the complete source code and documentation for a full-stack IoT system designed for real-time water level monitoring and remote control. The project uses an ESP8266-based hardware node and a native Android application, demonstrating a practical and scalable solution for automated resource management, suitable for academic and practical implementation.

Table of Contents

Key Features

System Architecture

Hardware Implementation

Communication Protocol

Software Implementation (Android App)

Tech Stack

How to Use

Key Features

Real-Time Monitoring: Live data stream of water level (percentage and cm) and water temperature, displayed with color-coded icons for at-a-glance status.

Remote & Automated Control: Manually control the water pump (ON/OFF), switch to an automated threshold-based mode (AUTO), and an emergency STOP override for maintenance.

Persistent Background Service: The Android app runs a Foreground Service to ensure 24/7 monitoring and can send push notifications for critical alerts (tank full/low), even when the app is closed.

Historical Data Dashboard: A comprehensive dashboard visualizes historical data with a combined bar chart (pump usage) and line chart (temperature), allowing for trend analysis.

Data-Driven Sensor Fusion: A calibrated linear regression model combines data from ultrasonic and capacitive sensors to significantly improve water level measurement reliability.

On-Device Persistence: Critical thresholds set via the app are saved to the ESP8266's EEPROM, allowing it to function autonomously after a power cycle.

System Architecture

The system is designed with a decoupled, three-tier architecture, a standard and scalable model for modern IoT applications.

Hardware Tier (The Device): An ESP8266 microcontroller acts as the edge device. It is responsible for reading sensor data, controlling the pump, and communicating with the cloud.

Communication Tier (The Cloud): A public MQTT broker (broker.hivemq.com) serves as the central communication hub, enabling efficient, low-latency messaging.

Software Tier (The Application): A native Android application provides the user interface for monitoring, control, and data visualization.

Hardware Implementation

The embedded system is the core of the data collection and local control logic. It is built using an ESP8266, multiple sensors, and actuators.

Sensor Array

HC-SR04 Ultrasonic Sensor: Measures the distance from the top of the tank to the water's surface.

Capacitive Soil Moisture Sensor: Submerged in the water, its analog output changes based on the water level.

DS18B20 Temperature Sensor: A waterproof digital sensor that provides accurate water temperature readings.

Sensor Fusion Model

A multiple linear regression model was developed by calibrating the two water level sensors against manually measured ground-truth data (Tank_dataset.csv). This data-driven approach compensates for individual sensor inaccuracies, yielding a more reliable measurement.

predicted_level = (0.002772 * capacitive_raw) + (-0.901014 * ultrasonic_cm) + 9.136660

Communication Protocol

The system uses the MQTT protocol for its lightweight and efficient publish-subscribe messaging pattern.

Topic Hierarchy

iot-projects/water-tank-123/data: For publishing sensor data from the device to the app.

iot-projects/water-tank-123/command: For publishing control commands from the app to the device.

iot-projects/water-tank-123/config: For publishing configuration settings from the app to the device.

Data Payload (JSON)

{
  "level_cm": 5.7,
  "level_percentage": 56,
  "pump_status": "OFF",
  "temperature": 28.5
}
