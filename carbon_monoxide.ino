#include <SoftwareSerial.h> // used by display

#define PROX_SENSOR_PIN         6
#define BUZZER_PIN              9
#define SCREEN_RX_PIN           10 // not used
#define SCREEN_TX_PIN           11
#define CO_SENSOR_ANALOGUE_PIN  0

#define ALARM_THRESHOLD         400 // in ppm
#define ALARM_TIMEOUT           120 // in seconds
#define ALARM_RESPONSE_TIME     15  // in minutes

const unsigned long CO_SENSOR_REFRESH_INTERVAL = 500ul;
const unsigned long ANIM_REFRESH_INTERVAL = 500ul;
const unsigned long SCREEN_ALARM_FLASH_INTERVAL = 400ul;
const unsigned long SCREEN_REFRESH_INTERVAL = 500ul;

unsigned long lastProxMillis = 0ul;
unsigned long lastCoMillis = 0ul;
unsigned long lastScreenMillis = 0ul;
unsigned long lastAnimMillis = 0ul;
unsigned long lastScreenAlarmMillis = 0ul;

// We're gonna want the audible alarm to sound, only after it's been 20 minutes of dangerous CO
// levels. We're going to write since when the CO levels have been dangerous here. Once it has have
// ALARM_RESPONSE_TIME minutes, then we're gonna sound the alarm.
unsigned long reachedAlarmThresholdMillis = 0ul;

// <screen>
#define SCREEN_BAUDRATE         9600
#define SCREEN_BRIGHTNESS       0x9D // 0x9D means fully on
#define SCREEN_TIMEOUT          60 // in seconds

unsigned long screenOnTimestamp = 0ul;
bool screenOn = false;
bool screenOffFlash = false;

SoftwareSerial screen(SCREEN_RX_PIN, SCREEN_TX_PIN);
// </screen>

// <screen_animation>
char animationChars[] = "^<^>"; // will be last symbol on line 2
int animationIndex = 0;
// </screen_animation>

// <co_sensor>
int coSensorValue = 0; // for caching inbetween reads
// </co_sensor>

// <buzzer>
int buzzerTone = 1500; // 1.5 kHz
unsigned long alarmOnTimestamp = 0ul;
bool inAlarm = false;
bool alarmFired = false; // so that we know when it has timed out
// </buzzer>

// <common>
unsigned long second = 1000ul; // in milliseconds
// </common>

void setup() {
     // Serial.begin(9600); // for debugging purposes

    // <screen>
    screen.begin(SCREEN_BAUDRATE);
    turnScreenOn(0);
    // </screen>

    // <prox_sensor>
    pinMode(PROX_SENSOR_PIN, INPUT);
    // </prox_sensor>
}

void loop() {
    int coReading = getCOReading();
    if (!inAlarm && !alarmFired && coReading >= ALARM_THRESHOLD) {
        if (needAudibleAlarm(coReading)) {
            fireAlarm();
            turnScreenOn(coReading);
        }
    } else if (inAlarm) {
        // OK, so alarm is on - should we turn it off?
        // if (coReading < ALARM_THRESHOLD) {
        //     forgoAlarm();
        // }

        if (getSecsSinceAlarmOn() >= ALARM_TIMEOUT) {
            alarmFired = true;
            forgoAlarm();
        }
    }

    if (coReading < ALARM_THRESHOLD) {
        alarmFired = false;

        if (screenOn && screenOffFlash) {
            initScreenStaticValues();
        }
    }

    // update screen if neccessary
    if (!screenOn && digitalRead(PROX_SENSOR_PIN) == LOW) {
        turnScreenOn(coReading);
    } else if (screenOn) {
        refreshScreenDynamicValues(coReading);
        refreshScreenAnimation();

        // OK, so screen is ON - should we turn it off?
        if (getSecsSinceScreenOn() >= SCREEN_TIMEOUT) {
            turnScreenOff();
        }

        // If we're in an alarm, we should flash the first (static) line of the screen
        if (inAlarm) {
            refreshScreenInAlarm();
        }
    }
}

bool needAudibleAlarm(int coReading) {
    if (coReading < ALARM_THRESHOLD) {
        reachedAlarmThresholdMillis = 0ul;
        return false;
    }

    if (reachedAlarmThresholdMillis == 0ul) {
        reachedAlarmThresholdMillis = millis();
        return false;
    }

    unsigned long alarmResponseTimeInMillis = ALARM_RESPONSE_TIME * second * 60;
    if (millis() - reachedAlarmThresholdMillis > alarmResponseTimeInMillis) {
        Serial.println("need alarm");
        return true;
    }

    Serial.println("don't need alarm");
    return false;
}

void fireAlarm() {
    inAlarm = true;
    alarmOnTimestamp = millis();

    tone(BUZZER_PIN, buzzerTone);
}

void forgoAlarm() {
    inAlarm = false;
    alarmOnTimestamp = 0ul;

    noTone(BUZZER_PIN);
}

int getSecsSinceAlarmOn() {
    if (!inAlarm) {
        return 0;
    }

    return (millis() - alarmOnTimestamp) / second;
}

int getCOReading() {
    unsigned long now = millis();
    if (now - lastCoMillis < CO_SENSOR_REFRESH_INTERVAL) {
       return coSensorValue;
    }

    lastCoMillis = now;
    coSensorValue = analogRead(CO_SENSOR_ANALOGUE_PIN);

    return coSensorValue;
}

void initScreenStaticValues() {
    initScreenSecondLine();

    // first line is a bit trickier, as it has dynamic values - we'll write the static ones first
    // and then at some point call the refresh method, to get the current data
    //     '1000ppm / 999s  '

    changeCursorPosition(0);
    screen.write("    ppm /    s ");
}

void initScreenSecondLine() {
    // the second line is easy, as it's static
    //     'CO bad @>400/15m'

    changeCursorPosition(16);
    screen.write(("CO bad @>" + String(ALARM_THRESHOLD) + "/" + String(ALARM_RESPONSE_TIME) + "m").c_str());
}

void refreshScreenDynamicValues(int coReading) {
    unsigned long now = millis();

    // Let's first see if we need to refresh the screen as per its refresh interval
    if (now - lastScreenMillis < SCREEN_REFRESH_INTERVAL) {
        return;
    }

    lastScreenMillis = now;

    // We need to update the ppm reading, as well as the seconds remaining till screen timeout.
    // Both get written on the second line.
    //     '1000ppm / 999s  '

    // update the CO reading
    writeValueRightToLeft(String(coReading), 3, 4);

    // update the timeout seconds
    int timeout = SCREEN_TIMEOUT - getSecsSinceScreenOn();
    writeValueRightToLeft(String(timeout), 12, 3);
}

void refreshScreenInAlarm() {
    unsigned long now = millis();

    // Let's first see if we need to refresh the screen as per its refresh interval
    if (now - lastScreenAlarmMillis < SCREEN_ALARM_FLASH_INTERVAL) {
        return;
    }

    lastScreenAlarmMillis = now;

    if (screenOffFlash) {
        initScreenSecondLine();
        screenOffFlash = false;
    } else {
        changeCursorPosition(16);
        screen.write("                ");
        screenOffFlash = true;
    }
}

void refreshScreenAnimation() {
    unsigned long now = millis();

    // Let's first see if we need to refresh the animation as per its refresh interval
    if (now - lastAnimMillis < ANIM_REFRESH_INTERVAL) {
        return;
    }

    lastAnimMillis = now;

    // update the animation (last symbol on line)
    changeCursorPosition(15);
    screen.write(animationChars[animationIndex]);

    animationIndex++;
    if (animationIndex == sizeof(animationChars) - 1) {
        animationIndex = 0;
    }
}

int getSecsSinceScreenOn() {
    if (!screenOn) {
        return 0;
    }

    return (millis() - screenOnTimestamp) / second;
}

void turnScreenOn(int coReading) {
    screenOnTimestamp = millis();
    screenOn = true;

    clearScreen();

    // turn display on
    screen.write(0xFE);
    screen.write(0x0C);

    // turn backlight on
    screen.write(0x7C);
    screen.write(SCREEN_BRIGHTNESS);
    delay(5); // otherwise it doesn't take

    initScreenStaticValues();
    refreshScreenDynamicValues(coReading);
}

void turnScreenOff() {
    screenOn = false;

    clearScreen();

    // turn display off
    screen.write(0xFE);
    screen.write(0x08);

    // turn backlight off
    screen.write(0x7C);
    screen.write(0x80); // off
    delay(5); // otherwise it doesn't take
}

void clearScreen() {
    screen.write(0xFE);
    screen.write(0x01);
}

void changeCursorPosition(int pos) {
    // second line begins from 64
    if (pos > 15) {
      pos = (pos - 16) + 64;
    }

    screen.write(0xFE);
    screen.write(pos + 128);
}

void writeValueRightToLeft(String val, int pos, int maxLen) {
    for (int i = val.length() - 1, j = 0, len = 0; len < maxLen; i--, j++, len++) {
        changeCursorPosition(pos - j);

        if (i >= 0) {
            screen.write(val[i]);
        } else {
            screen.write(" ");
        }
    }
}

