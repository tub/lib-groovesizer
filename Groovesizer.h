#ifndef GROOVESIZER_H
#define GROOVESIZER_H

#include <Arduino.h>

class Groovesizer {
    public:
        Groovesizer();
        ~Groovesizer();
        void setLed(byte row, byte col, bool val);
        void setLedRow(byte row, byte val);
        bool getButton(byte row, byte col);
        void read();
        void write();
        static const byte MATRIX_ROWS = 5;
        static const byte MATRIX_COLS = 8;
        static const byte NUM_POTS = 6;
		    void setButtonUpListener(void (*buttonUpCallback)(byte col, byte row, unsigned int millisHeld));
		    void setButtonDownListener(void (*buttonDownCallback)(byte col, byte row));
        void setPotChangeListener(void (*potChangeCallback)(byte pot, int val));

    private:
        void writeLeds();
        void readButtons();
        void readPots();
        int getPotValue(byte channel);     
        byte shiftIn(byte dataPin, byte clockPin);
        void (*_buttonUpCallback)(byte row, byte col, unsigned int millisHeld);
        void (*_buttonDownCallback)(byte row, byte col);
        void (*_potChangeCallback)(byte pot, int val);

};

#endif
