#include <core.h>
#include <scif.h>
#include <pm.h>
#include <gpio.h>
#include <spi.h>
#include <stdio.h>
#include "cansat_launcher_remote.h"
#include "pins.h"
#include "drivers/oled_ssd1306/oled.h"
#include "drivers/lora/lora.h"
#include "logo_plasci.h"



enum class Command {
    NONE = 0,
    ACK = 1,
    GET_STATUS = 2,
    STATUS = 3,
    OPEN = 4,
    CLOSE = 5,
};

const int TURNON_DELAY = 1000;
const int TURNOFF_DELAY = 1000;
const int LED_BLINK_DELAY = 2000;
const int DELAY_LOGO_INIT = 1000;

const int N_LAUNCHERS = 3;
const char* LAUNCHERS_NAMES[N_LAUNCHERS] = {
    "1 - VERT",
    "2 - ROUGE",
    "3 - JAUNE"
};

void sendCommand(int currentLauncher, Command command);

int main() {
    // Init the microcontroller
    Core::init();
    SCIF::enableRCFAST(SCIF::RCFASTFrequency::RCFAST_12MHZ);
    PM::setMainClockSource(PM::MainClockSource::RCFAST);
    Error::setHandler(Error::Severity::WARNING, warningHandler);
    Error::setHandler(Error::Severity::CRITICAL, criticalHandler);

    // Power
    Core::sleep(TURNON_DELAY);
    GPIO::enableOutput(PIN_PW_EN, GPIO::HIGH);

    // Enable the SPI interface for the OLED and the LoRa
    SPI::setPin(SPI::PinFunction::MISO, PIN_MISO);
    SPI::setPin(SPI::PinFunction::MOSI, PIN_MOSI);
    SPI::setPin(SPI::PinFunction::SCK, PIN_SCK);
    SPI::setPin(SPI::PinFunction::CS0, PIN_CS0);
    SPI::setPin(SPI::PinFunction::CS1, PIN_CS1);
    SPI::enableMaster();

    // Init the GUI
    // Init the screen
    OLED::initScreen(0, PIN_OLED_DC, PIN_OLED_RES);
    OLED::setRotation(OLED::Rotation::R180);
    OLED::setContrast(255);
    OLED::printXXLarge((OLED::WIDTH - 64) / 2, (OLED::HEIGHT - 64) / 2, ICON_LOGO_PLASCI);
    OLED::setSize(Font::Size::MEDIUM);
    OLED::printCentered(OLED::WIDTH / 2, 54, "CanSat launcher");
    OLED::refresh();

    // Init the buttons
    GPIO::enableInput(PIN_BTN_UP, GPIO::Pulling::PULLUP);
    GPIO::enableInput(PIN_BTN_DOWN, GPIO::Pulling::PULLUP);
    GPIO::enableInput(PIN_BTN_LEFT, GPIO::Pulling::PULLUP);
    GPIO::enableInput(PIN_BTN_RIGHT, GPIO::Pulling::PULLUP);
    GPIO::enableInput(PIN_BTN_OK, GPIO::Pulling::PULLUP);
    GPIO::enableInput(PIN_BTN_PW);
    GPIO::enableInput(PIN_BTN_TRIGGER, GPIO::Pulling::PULLUP);

    // Init the leds
    GPIO::enableOutput(PIN_LED_PW, GPIO::LOW);
    GPIO::enableOutput(PIN_LED_TRIGGER, GPIO::HIGH);

    // Init the LoRa
    LoRa::setPin(LoRa::PinFunction::RESET, PIN_LORA_RESET);
    SPI::setPin(static_cast<SPI::PinFunction>(static_cast<int>(SPI::PinFunction::CS0) + static_cast<int>(SPI_SLAVE_LORA)), PIN_LORA_CS);
    if (!LoRa::init(SPI_SLAVE_LORA, 869350000L)) {
        criticalHandler();
    }
    LoRa::setTxPower(10); // dBm
    LoRa::setSpreadingFactor(10);
    LoRa::setCodingRate(LoRa::CodingRate::RATE_4_8);
    LoRa::setBandwidth(LoRa::Bandwidth::BW_62_5kHz);
    LoRa::setExplicitHeader(true);
    LoRa::enableRx();

    // Give the user some time to admire the pixel-art logo
    Core::sleep(DELAY_LOGO_INIT);


    // Current state
    bool init = true;
    bool lastBtnPw = true;
    Core::Time t = Core::time();
    Core::Time tPowerLed = t;
    Core::Time tBtnPwPressed = 0;
    int currentLauncher = 0;
    Core::Time tCommandSent = 0;
    Core::Time tAckReceived = 0;
    Command currentCommand = Command::NONE;
    bool ackReceived = false;
    bool commandFailed = false;
    int currentRepeat = 0;
    const unsigned long DELAY_MIN_BETWEEN_COMMANDS = 400;
    const unsigned long DELAY_COMMAND_TIMEOUT = 1000;
    const int N_REPEAT_COMMANDS = 3;
    const unsigned long DELAY_ORDER_DISPLAYED = 1000;
    const char* LABEL_COMMAND_OPEN = "Ouverture...";
    const char* LABEL_COMMAND_CLOSE = "Fermeture...";
    const char* LABEL_COMMAND_OPEN_ACK = "OUVERTURE !";
    const char* LABEL_COMMAND_CLOSE_ACK = "FERMETURE !";
    const char* LABEL_COMMAND_FAILED = "Echec de la commande";
    Core::Time tTelem = 0;
    bool telemAvailable = false;
    bool hatchOpen = false;
    int altitude = 0;
    int temperature = 0;
    int batteryPercent = 0;
    const unsigned long DELAY_GET_STATUS = 3000;
    const unsigned long DELAY_GET_STATUS_TIMEOUT = 10000;
    Core::Time tGetStatus = 0;

    // Main loop
    while (1) {
        bool refresh = false;

        // Init
        if (init) {
            refresh = true;
            init = false;
        }

        // Power button
        bool btnPw = GPIO::get(PIN_BTN_PW);
        if (!lastBtnPw && btnPw) {
            // Button pressed
            t = Core::time();
            tBtnPwPressed = t;
        } else if (!btnPw) {
            // Button released
            tBtnPwPressed = 0;
        }
        if (tBtnPwPressed > 0 && Core::time() - tBtnPwPressed >= TURNOFF_DELAY) {
            // Shutdown

            // Turn on the power LED
            GPIO::set(PIN_LED_PW, GPIO::LOW);

            // Display the shutdown message on the screen
            OLED::clear();
            OLED::printXXLarge((OLED::WIDTH - 64) / 2, (OLED::HEIGHT - 64) / 2, ICON_LOGO_PLASCI);
            OLED::setSize(Font::Size::MEDIUM);
            OLED::printCentered(OLED::WIDTH / 2, 54, "Bye!");
            OLED::refresh();
            OLED::refresh();

            // Wait a second
            Core::sleep(1000);

            // Turn off the screen and the power LED
            OLED::disable();
            GPIO::set(PIN_LED_PW, GPIO::HIGH);

            // Ready to shutdown, release the power supply enable line
            GPIO::set(PIN_PW_EN, GPIO::LOW);
        }

        // Power LED
        t = Core::time();
        GPIO::set(PIN_LED_PW, !(t - tPowerLed < 100));
        while (t - tPowerLed > LED_BLINK_DELAY) {
            tPowerLed += LED_BLINK_DELAY;
        }

        // Get the buttons state
        bool btnLeft = GPIO::fallingEdge(PIN_BTN_LEFT);
        bool btnRight = GPIO::fallingEdge(PIN_BTN_RIGHT);
        bool btnUp = GPIO::fallingEdge(PIN_BTN_UP);
        bool btnDown = GPIO::fallingEdge(PIN_BTN_DOWN);
        bool btnOk = GPIO::fallingEdge(PIN_BTN_OK);

        // Left and right buttons : select the launcher
        if ((btnLeft && currentLauncher > 0) || (btnRight && currentLauncher < N_LAUNCHERS - 1)) {
            if (btnLeft) {
                currentLauncher--;
            } else {
                currentLauncher++;
            }
            telemAvailable = false;
            tGetStatus = 0;
            currentCommand = Command::NONE;
            ackReceived = false;
            commandFailed = false;
            currentRepeat = 0;
            tAckReceived = 0;
            refresh = true;
        }

        // Up and down buttons : open and close the launcher
        if (btnUp || btnDown) {
            // If a command was sent recently, wait for a bit because the launcher
            // is probably occupied sending the answer
            t = Core::time();
            unsigned long dt = t - tGetStatus;
            if (dt < DELAY_MIN_BETWEEN_COMMANDS) {
                Core::sleep(DELAY_MIN_BETWEEN_COMMANDS - dt);
            }
            dt = t - tCommandSent;
            if (dt < DELAY_MIN_BETWEEN_COMMANDS) {
                Core::sleep(DELAY_MIN_BETWEEN_COMMANDS - dt);
            }

            // Send the command
            if (btnUp) {
                // Open
                sendCommand(currentLauncher, Command::OPEN);
                currentCommand = Command::OPEN;
            } else if (btnDown) {
                // Close
                currentCommand = Command::CLOSE;
                sendCommand(currentLauncher, Command::CLOSE);
            }
            tCommandSent = Core::time();
            ackReceived = false;
            commandFailed = false;
            currentRepeat = 0;
            tAckReceived = 0;
            refresh = true;
        }

        // Get launcher status from telem
        t = Core::time();
        if ((btnOk || t >= tGetStatus + DELAY_GET_STATUS) && currentCommand == Command::NONE) {
            sendCommand(currentLauncher, Command::GET_STATUS);
            tGetStatus = t;
        }

        // Get status and ack packets
        t = Core::time();
        if (LoRa::rxAvailable()) {
            // Retrieve this data
            const int BUFFER_RX_SIZE = 10;
            uint8_t rxBuffer[BUFFER_RX_SIZE];
            int rxSize = LoRa::rx(rxBuffer, BUFFER_RX_SIZE);

            // Check that this looks like a valid frame on the same channel
            if (rxSize >= 4
                    && rxBuffer[0] == 'c' && rxBuffer[1] == 's'
                    && rxBuffer[2] == currentLauncher) {
                Command command = static_cast<Command>(rxBuffer[3]);
                if (rxSize == 5 && command == Command::ACK && currentCommand != Command::NONE && !ackReceived && !commandFailed) {
                    // Ack
                    Command command2 = static_cast<Command>(rxBuffer[4]);
                    if (command2 == currentCommand) {
                        if (command2 == Command::CLOSE) {
                            hatchOpen = false;
                        } else if (command2 == Command::OPEN) {
                            hatchOpen = true;
                        }
                        ackReceived = true;
                        tAckReceived = t;
                        refresh = true;
                    }

                } else if (rxSize == 10 && command == Command::STATUS) {
                    hatchOpen = rxBuffer[4];
                    altitude = rxBuffer[5] << 8 | rxBuffer[6];
                    temperature = rxBuffer[7] << 8 | rxBuffer[8];
                    batteryPercent = rxBuffer[9];
                    tTelem = t;
                    telemAvailable = true;
                    refresh = true;
                }
            }
        }
        if (tTelem > 0 && Core::time() >= tTelem + DELAY_GET_STATUS_TIMEOUT) {
            tTelem = 0;
            telemAvailable = false;
            refresh = true;
        }

        // If no ack was received after some time, repeat the command
        t = Core::time();
        if (currentCommand != Command::NONE && !ackReceived && !commandFailed && tCommandSent > 0 && t >= tCommandSent + DELAY_COMMAND_TIMEOUT) {
            if (currentRepeat < N_REPEAT_COMMANDS - 1) {
                sendCommand(currentLauncher, currentCommand);
                tCommandSent = t;
                currentRepeat++;
                refresh = true;
            } else {
                commandFailed = true;
                refresh = true;
            }
        }

        // Timeout of messages displayed on screen
        t = Core::time();
        if (currentCommand != Command::NONE && ((ackReceived && t > tAckReceived + DELAY_ORDER_DISPLAYED) || (commandFailed && t > tCommandSent + DELAY_COMMAND_TIMEOUT + DELAY_ORDER_DISPLAYED))) {
            currentCommand = Command::NONE;
            ackReceived = false;
            commandFailed = false;
            tCommandSent = 0;
            currentRepeat = 0;
            tAckReceived = 0;
            refresh = true;
        }

        // Update screen
        if (refresh) {
            OLED::clear();
            OLED::setSize(Font::Size::LARGE);
            OLED::button(0, 0, OLED::WIDTH, 22, LAUNCHERS_NAMES[currentLauncher], true, false, currentLauncher > 0, currentLauncher < N_LAUNCHERS - 1);
            OLED::setSize(Font::Size::MEDIUM);
            if (telemAvailable) {
                OLED::print(5, 28, "Alt: ");
                OLED::printInt(altitude / 10);
                OLED::print(".");
                OLED::printInt(altitude % 10);
                OLED::print("m");
                if (hatchOpen) {
                    OLED::print(72, 28, "Ouvert");
                } else {
                    OLED::print(72, 28, "Ferm");
                    OLED::print((char)(95 + 32));
                }
                OLED::print(5, 40, "Temp: ");
                OLED::printInt(temperature / 10);
                OLED::print(".");
                OLED::printInt(temperature % 10);
                OLED::print("C");
                OLED::print(72, 40, "Batt: ");
                OLED::printInt(batteryPercent);
                OLED::print("%");
            } else {
                OLED::printCentered(OLED::WIDTH / 2, 34, "Telem non disponible");
            }
            if (currentCommand != Command::NONE) {
                if (ackReceived) {
                    if (currentCommand == Command::OPEN) {
                        OLED::printCentered(OLED::WIDTH / 2, 55, LABEL_COMMAND_OPEN_ACK);
                    } else if (currentCommand == Command::CLOSE) {
                        OLED::printCentered(OLED::WIDTH / 2, 55, LABEL_COMMAND_CLOSE_ACK);
                    }
                } else if (commandFailed) {
                    OLED::printCentered(OLED::WIDTH / 2, 55, LABEL_COMMAND_FAILED);
                } else {
                    if (currentCommand == Command::OPEN) {
                        OLED::printCentered(OLED::WIDTH / 2, 55, LABEL_COMMAND_OPEN);
                    } else if (currentCommand == Command::CLOSE) {
                        OLED::printCentered(OLED::WIDTH / 2, 55, LABEL_COMMAND_CLOSE);
                    }
                }
            } else {
                OLED::printCentered(OLED::WIDTH / 2, 55, "Haut : ouvrir | Bas : fermer");
            }
            OLED::refresh();
        }

        lastBtnPw = btnPw;

        Core::sleep(10);
    }
}

void sendCommand(int currentLauncher, Command command) {
    const int SIZE = 4;
    uint8_t buffer[SIZE] = {'C', 'S', static_cast<uint8_t>(currentLauncher), static_cast<uint8_t>(command)};
    LoRa::tx(buffer, SIZE);
}


void warningHandler(Error::Module module, int userModule, Error::Code code) {
    GPIO::set(PIN_LED_TRIGGER, GPIO::LOW);
    Core::sleep(100);
    GPIO::set(PIN_LED_TRIGGER, GPIO::HIGH);
    Core::sleep(100);
    GPIO::set(PIN_LED_TRIGGER, GPIO::LOW);
    Core::sleep(100);
    GPIO::set(PIN_LED_TRIGGER, GPIO::HIGH);
    Core::sleep(100);
    GPIO::set(PIN_LED_TRIGGER, GPIO::LOW);
    Core::sleep(100);
}

void criticalHandler(Error::Module module, int userModule, Error::Code code) {
    while (1) {
        GPIO::set(PIN_LED_TRIGGER, GPIO::LOW);
        Core::sleep(100);
        GPIO::set(PIN_LED_TRIGGER, GPIO::HIGH);
        Core::sleep(100);
    }
}
