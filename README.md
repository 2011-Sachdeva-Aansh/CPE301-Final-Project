# CPE301-Final-Project
Smart Energy-Saving Room Lighting System
Author: Team 9 - Aansh Sachdeva

Description
An automated room lighting system built on the Arduino Mega 2560 that uses an ultrasonic sensor to detect occupancy and a photoresistor to measure ambient light. The system automatically controls a room LED based on these inputs using a Finite State Machine (FSM) with four states: OFF, IDLE, ACTIVE, and ERROR.

Components | Purpose
- ATMega 2560 | Main microcontroller
- HC-SR04 Ultrasonic Sensor | Detects room occupancy
- Photoresistor (LDR) + 10kΩ resistor | Measures ambient light level
- 16x2 LCD Display | Shows system state and sensor readings
- RTC DS1307 | Timestamps all system events
- Push Button x 3 | ON, OFF, RESET controls
- LED x 5 (Red, Yellow, Green, Blue, White) | State indicators + room light (white)
- 220Ω Resistors x 5 | LED current limiting
- 10kΩ Potentiometer | LCD contrast control
- Breadboard + Jumper Wires | Circuit connections

Pin Map - Component | Pin
- HC-SR04 TRIG | Pin 9 (PH6) 
- HC-SR04 ECHO | Pin 10 (PB4)
-  LDR | A0 
- CD RS | Pin 7 
- LCD EN | Pin 6 
- LCD D4-D7 | Pins 3, 4, 5, 8 
- RTC SDA | Pin 20 
- RTC SCL | Pin 21 
- LED OFF (Red) | Pin 22 (PA0) 
- LED IDLE (Yellow) | Pin 23 (PA1) 
- LED ACTIVE (Green) | Pin 24 (PA2) 
- LED ERROR (Blue) | Pin 25 (PA3) 
- Room LED | Pin 13 (PB7) 
- ON Button | Pin 2 (PE4 - INT4) 
- OFF Button | Pin 30 (PC7) 
- RESET Button | Pin 12 (PB6)

FSM States - State | LED | Condition
- OFF | Red | System powered down
- IDLE | Yellow | Monitoring sensors
- ACTIVE | Green + Room LED | Room occupied AND dark
- ERROR | Blue | Sensor fault detected

Run
- Open smart_lighting.ino
- Click Upload
- Open Serial Monitor at 9600 baud

Implementation Notes
- All GPIO controlled via direct register manipulation (DDRx, PORTx, PINx)
- UART implemented via UCSR0/UDR0 registers
- ADC implemented via ADMUX/ADCSRA registers
- ON button uses hardware external interrupt via attachInterrupt()
- 1-minute sensor update interval implemented using millis()

Video Demo
/////////
