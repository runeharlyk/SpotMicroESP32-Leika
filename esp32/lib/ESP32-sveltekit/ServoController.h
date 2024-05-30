#include <Adafruit_PWMServoDriver.h>
#include <DeviceConfigurationService.h>

class ServoController : public Adafruit_PWMServoDriver {
   public:
    ServoController(DeviceConfigurationService deviceConfigurationService)
        : Adafruit_PWMServoDriver(), _config(deviceConfigurationService) {
        begin();
    }

    void configure() {
        setOscillatorFrequency(_config.servo_oscillator_frequency());
        setPWMFreq(_config.servo_pwm_frequency());
    }

    void deactivate() {
        isActive = false;
        sleep();
    }

    void activate() {
        isActive = true;
        sleep();
    }

    bool isActive{false};

   private:
    DeviceConfigurationService _config;
};
