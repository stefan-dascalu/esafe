# eSafe

**Digital Lockbox with Arduino Uno**

I built eSafe as a DIY electronic safe that brings together multiple embedded-systems concepts into one project. It offers:

- **4×4 matrix keypad** for entering a secure 4-digit PIN  
- **SG90 servo** to drive a latch (locked/unlocked angles)  
- **Passive buzzer & red/green LEDs** for alarm and status feedback  
- **DS1307 real-time clock + 16×2 I²C LCD** to enforce business-hour access windows and log the last unlock time  
- **Time-lock feature**: prevents unlocking outside a configurable time window  
- **Alarm** triggered after three wrong PIN attempts (5 s buzzer + flashing red LED)  

### Key Technical Highlights

- **GPIO & pin-change interrupts** implement keypad scanning  
- **Timer 2 CTC mode** handles keypad debounce and updates the clock display every second  
- **PWM** via the Arduino Servo library controls latch positioning precisely  
- **I²C/TWI bus sharing** lets the RTC and LCD coexist on A4/A5  

For full schematics, bill of materials, code structure, and documentation, visit my OCW project page:

 https://ocw.cs.pub.ro/courses/pm/prj2025/eradu/stefan.dascalu2612  
