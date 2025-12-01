# 🔐 Automatic Gate System with Emergency Blocking
**ESP32 + FreeRTOS — Multicore Task, Queue, Semaphore, ISR, PWM Servo & Buzzer**

.............................................................................................................

Project ini merupakan sistem otomatisasi pintu gerbang yang berjalan pada *dual-core ESP32* menggunakan FreeRTOS. Sistem dapat membuka/menutup pintu menggunakan servo, menampilkan status lewat LED, memberi peringatan melalui buzzer, dan memiliki fitur **Emergency Lock** untuk memblokir seluruh perintah OPEN/CLOSE.

.............................................................................................................

## 📦 Komponen yang Digunakan

| Komponen                | Fungsi                                       |
|-------------------------|---------------------------------------------|
| ESP32 Dev Board         | Proses utama, menjalankan multitasking RTOS |
| Servo Motor (SG90/MG996)| Membuka dan menutup gerbang                  |
| Active/Passive Buzzer   | Memberikan alarm dan bunyi status           |
| 3 Tombol Push Button    | OPEN, CLOSE, dan EMERGENCY toggle           |
| 2 LED (Merah & Hijau)   | Indikator status pintu terbuka/tertutup     |
| Resistor                | Untuk pull-up button jika dibutuhkan        |
| Catu Daya 5V            | Power servo dan ESP32                        |

**GPIO Mapping**

| Fungsi         | GPIO |
|----------------|------|
| BTN_OPEN       | 12   |
| BTN_CLOSE      | 13   |
| BTN_EMERGENCY  | 14   |
| LED_OPEN       | 2    |
| LED_CLOSE      | 4    |
| Buzzer         | 18   |
| Servo PWM      | 15   |

.............................................................................................................

## 🛠 Fitur Utama

- Buka/Tutup gerbang via button  
- Mode Emergency (toggle) → semua perintah OPEN/CLOSE diblokir, servo berhenti, queue dikosongkan  
- Multicore Processing: Core 0 → Servo + Buzzer, Core 1 → Emergency Monitor + LED  
- ISR Button untuk input cepat tanpa blocking  
- Debug lengkap via Serial Monitor  

.............................................................................................................

## 🔗 Arsitektur Komunikasi Antar Task

### 📨 1. Queue — komunikasi OPEN/CLOSE ke ServoTask
- ISR mengirim perintah `CMD_OPEN` / `CMD_CLOSE` ke **gateQueue**  
- Servo task menunggu dan memproses perintah ini  
- Queue dibersihkan saat emergency aktif

### 🔒 2. Mutex — proteksi eksklusif servo
- Servo tidak boleh dikendalikan dua task sekaligus  
- ServoTask mengambil mutex sebelum menggerakkan servo  
- EmergencyMonitorTask bisa menghentikan servo dengan mutex  

### 🚨 3. Binary Semaphore — Emergency ON/OFF
- ISR tombol emergency memanggil `xSemaphoreGiveFromISR()`  
- EmergencyMonitorTask menangkap sinyal tersebut  
- Toggle otomatis ON/OFF

### 🔁 4. Shared Variable
- `emergency_activated` → status emergency  
- `gate_is_open` → status servo  
- `blocked_count` → jumlah perintah diblokir  

.............................................................................................................

## ⚙️ Metode yang Dipakai

| Task                 | Core | Fungsi                        | Prioritas |
|----------------------|------|-------------------------------|-----------|
| Servo Task           | 0    | Menangani OPEN/CLOSE          | 3         |
| Buzzer Task          | 0    | Mode alarm                     | 2         |
| Emergency Monitor    | 1    | Mengawasi tombol emergency     | 4         |
| LED Task             | 1    | Indikator status               | 1         |

- ISR Button untuk input cepat  
- PWM Servo (50Hz) & Buzzer (dynamik freq)  
- Emergency blocking → queue dibersihkan, servo berhenti, LED & buzzer warning mode  

.............................................................................................................

## 🧩 Input dan Output Sistem

### Input
- BTN_OPEN → kirim CMD_OPEN  
- BTN_CLOSE → kirim CMD_CLOSE  
- BTN_EMERGENCY → toggle emergency mode  

### Output
- Servo Motor → 0° (close) / 90° (open), pergerakan cepat bertahap  
- Buzzer → Normal = silent, Emergency = beep cepat  
- LED → Hijau = open, Merah = closed, Emergency = kedip cepat  
- Serial Monitor → debug semua aktivitas  

.............................................................................................................

## 🚀 Cara Kerja Sistem

1. Tekan **OPEN** → ISR → Queue → Servo buka  
2. Tekan **CLOSE** → ISR → Queue → Servo tutup  
3. Tekan **EMERGENCY** → Servo berhenti, queue dikosongkan, LED & buzzer mode darurat  
4. Tekan **EMERGENCY lagi** → Sistem kembali normal  

.............................................................................................................


**Wiring simulasi di wokwi**
<img width="741" height="639" alt="image" src="https://github.com/user-attachments/assets/f1cd5d82-b4d3-4e9f-b74e-804f3bc0d16c" />


**Video simulasi**


https://github.com/user-attachments/assets/45314a1e-c9ea-45d7-b2e6-00caf5a18394


