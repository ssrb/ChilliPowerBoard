#include "Arduino.h"
#include <avr/io.h> 
 
#include <Wire.h>
#include <PCF8583.h>
#include <EEPROM.h>

// bitlash
#include <bitlash.h>
extern void sp(const char *str);
extern void speol(void);
extern byte findscript(char *);
#define RegisterBitlashFunction(function) addBitlashFunction(#function, (bitlash_function)function)

#include "mario.h"

const int kE2UserReserved = 17 * 8;
const byte kMaxFailure = 10;

PCF8583 theRTClock(0xA0);

class CronTab {
  public:
    CronTab(int e2start, int e2amount) :
      _e2crontab(e2start),
      _maxNbJob(e2amount / kE2RecordSize)
    {
    }
  
    void run() const {
      theRTClock.get_time();
      char now[] = { theRTClock.second , theRTClock.minute, theRTClock.hour, theRTClock.get_day_of_week(), theRTClock.day, theRTClock.month, theRTClock.year / 100};
      for (int jobIdx = 0; jobIdx < _maxNbJob; ++jobIdx) {
          char schedule[kNbField];
          E2Read(getScheduleAddr(jobIdx), schedule, sizeof(schedule));
          if (schedule[0] == kJobNotSet) {
            continue;
          }
          if (JobMustBeDone(schedule, now)) {
            char command[kCmdMaxSize + 1];
            command[sizeof(command) - 1] = 0;
            E2Read(getCommandAddr(jobIdx), command, sizeof(command) - 1);
            doCommand(command);
          }
      }
    }
  
    void addJob(const char schedule[], const char *command, int jobIdx) {
        if (command && strlen(command) <= kCmdMaxSize) {
          if (0 <= jobIdx && jobIdx < _maxNbJob) {
            E2Write(getScheduleAddr(jobIdx), schedule, kNbField);
            char paddedCommand[kCmdMaxSize];
            strncpy(paddedCommand, const_cast<char *>(command), sizeof(paddedCommand));
            E2Write(getCommandAddr(jobIdx), paddedCommand, sizeof(paddedCommand));
          }
        }
    }
    
    void delJob(int jobIdx) {
      if (0 <= jobIdx && jobIdx < _maxNbJob) {
        eraseCronJobInE2(jobIdx);
      }
    }
    
    void erase() {
      for (int jobIdx = 0; jobIdx < _maxNbJob; ++jobIdx) {
        eraseCronJobInE2(jobIdx);
      }
    }
    
    void ls() const {
      char buff[10];
  
      sp("JOB|");
      for (int field = 0; field < kNbField; ++field) {
        sprintf(buff, "%4s ", FieldStr[field]);
        sp(buff);
      }
      sp("| CMD");
      speol();
      
      for (int jobIdx = 0; jobIdx < _maxNbJob; ++jobIdx) {
          char schedule[kNbField];
          E2Read(getScheduleAddr(jobIdx), schedule, sizeof(schedule));
        
          if (schedule[0] == kJobNotSet) {
            continue;
          }
          
          char command[kCmdMaxSize + 1];
          command[sizeof(command) - 1] = 0;
          E2Read(getCommandAddr(jobIdx), command, sizeof(command) - 1); 
          
          sprintf(buff, "%2d ", jobIdx);
          sp(buff);
          sp("|");
          for (int field = 0; field < kNbField; ++field) {
              if (schedule[field] == -1) {
                sp("   * ");
              } else if (field == kDayOfWeek) {
                sprintf(buff, "  %02X ", ((byte *)schedule)[field] & 0x7F);
                sp(buff);
              } else {
                if (schedule[field] >= 0) {
                  sprintf(buff, "  %2d ", schedule[field]);
                  sp(buff);
                } else {
                  sprintf(buff, "*/%2d ", -schedule[field]);
                  sp(buff);
                }
              }
          }
          sp("| ");
          sp(command);
          speol();
      }
    }
    
    enum Field {
          kSecond,
          kMinute,
          kHour,
          kDayOfWeek,
          kDayOfMonth,
          kMonth,
          kYear,
          kNbField
    };
     
    private:
    
     static const char *FieldStr[];
     
     static bool JobMustBeDone(const char schedule[], const char now[]) {
        for (int field = 0; field < kNbField; ++field) {
          if (field == kDayOfWeek) {
            if (!((byte)schedule[field] & (1 << now[field]))) {
              return false;
            }
          } else {
            if (schedule[field] >= 0) {
              if (now[field] != schedule[field]) {
                return false;
              }
            } else {
              if (now[field] % -schedule[field]) {
                return false;
              }
            }
          }
        }
        return true;
     }  
      
     static void E2Write(int addr, const char *src, int n) {
       for (int i = 0; i < n; ++i) {
         EEPROM.write(addr + i, src[i]);
       }
      }
  
     static void E2Read(int addr, char *dst, int n) {
         for (int i = 0; i < n; ++i) {
           dst[i] = EEPROM.read(addr + i);
         }
      }
    
    void eraseCronJobInE2(int jobIdx) {
      int jobAddr = _e2crontab + jobIdx * kE2RecordSize;
      for (int addr = jobAddr; addr < (jobAddr + kE2RecordSize); ++addr) {
        EEPROM.write(addr, kJobNotSet);
      }
    }
    
    int getScheduleAddr(int jobIdx) const {
       return _e2crontab + jobIdx * kE2RecordSize;
    }
    
    int getCommandAddr(int jobIdx) const {
      return getScheduleAddr(jobIdx) + kNbField;
    }
    
    const int _e2crontab;
    const int _maxNbJob;
    
    static const int kCmdMaxSize  = 10;
    static const int kE2RecordSize = kNbField + kCmdMaxSize;
    
    static const int kJobNotSet = -100;
};

const char *CronTab::FieldStr[] = {"s", "m", "h", "dow", "dom", "M", "Y"};
     
CronTab theCronTab(E2END - kE2UserReserved, kE2UserReserved);

// Bitlash user functions
numvar crond() {
  theCronTab.run();
  return 0;
}

numvar addcronjob() {
  if (getarg(0) == 9) {
    theCronTab.addJob(
      (char[]){(char)getarg(1), (char)getarg(2), (char)getarg(3), (char)getarg(4), (char)getarg(5), (char)getarg(6), (char)getarg(7)}, 
      (const char *)getstringarg(8),
      getarg(9));
  }
  return 0;
}

numvar delcronjob() {
  for (int argIdx = 1; argIdx <= getarg(0); ++argIdx) {
    theCronTab.delJob(getarg(argIdx));
  }
  return 0;
}

numvar erasecrontab() {
  if (!getarg(0)) {
    theCronTab.erase();
  }
  return 0;
}

numvar lscrontab() {
  theCronTab.ls();
  return 0;
}

numvar time() {
    if(getarg(0) == 0) {
      static const char* DayStrings[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
      theRTClock.get_time();
      char t[50];
      sprintf_P(t, PSTR(" %04d/%02d/%02d %02d:%02d:%02d"), theRTClock.year, theRTClock.month, theRTClock.day, theRTClock.hour, theRTClock.minute, theRTClock.second);
      sp(DayStrings[theRTClock.get_day_of_week()]); sp(t); speol();
    } else if(getarg(0) == 6) {
       theRTClock.year= getarg(1);
       theRTClock.month = getarg(2);
       theRTClock.day = getarg(3);
       theRTClock.hour = getarg(4);
       theRTClock.minute = getarg(5);
       theRTClock.second = getarg(6);    
       theRTClock.set_time();
    }
   return 0;
}

// Little helper
namespace {
  int e2strcmp(int addr, const char *str) {
    char c;
     for (; addr < E2END && *str; ++addr, ++str) {
        if (*str != (c = EEPROM.read(addr))) {
          return c < *str ? -1 : 1;
        }
      }
      return 0;
   }
}

bool CronDaemonIsRunning() {
  extern int tasklist[];
  for (int slot = 0; slot < 10; ++slot) {
    if (tasklist[slot] != -1 && !e2strcmp(tasklist[slot], "cronsvc")) {
       return true;
    }
  }
  return false;
}

void UnleashWatchDog() {
  static byte failOmeter = 0;
  if (failOmeter < kMaxFailure) {
    if (!CronDaemonIsRunning()) {
        ++failOmeter;
        // Start the cron daemon
        doCommand("startcrond");
      } else {
        failOmeter = 0;
      }
    } else {
      static bool failHandlerCalled = false;
      if (!failHandlerCalled && findscript("onmaxfailure")) {               
        doCommand("onmaxfailure");
        failHandlerCalled = true;
      }
      // Notify the powerboard needs some love every minutes
      NotifyPowerBoardNeedsSomeLove(60);
  }
}

void NotifyPowerBoardNeedsSomeLove(int intervalsec) {
  static int nextNotif = millis() / 1000;
  int now = millis() / 1000;
  if (nextNotif <= now) {
    sing(9, melody, tempo, sizeof(melody) / sizeof(melody[0]));
    nextNotif = millis() / 1000 + intervalsec;
  }
}

void setup(void) {
  
        pinMode(2, OUTPUT);
        pinMode(3, OUTPUT);
        pinMode(4, OUTPUT);
        pinMode(5, OUTPUT);
  
	initBitlash(57600);

        // Register the PCF8583 RT clock based time function
        RegisterBitlashFunction(time);
        
        // Register the cron daemon callback
	RegisterBitlashFunction(crond);
        
        // Cron daemon user interface
        RegisterBitlashFunction(addcronjob);
        RegisterBitlashFunction(delcronjob);
        RegisterBitlashFunction(erasecrontab);
        RegisterBitlashFunction(lscrontab);

        // "run" can't call directly a user function
        doCommand("function cronsvc { crond; }");
        
        // Declare a start crond function, it will schedule 
        // a call to the cron function every second
        doCommand("function startcrond { crond; run cronsvc,1000; }");
}

void loop(void) {
        UnleashWatchDog();
        runBitlash();
}
