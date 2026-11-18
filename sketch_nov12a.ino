#include <C:\Users\kratu\Documents\robo\huskytwo.cpp>
#include <PTBOTAtomVX.h>

using namespace std;

Huskylens husky(&Wire);

void setup() {
    husky.begin();
    
    lcdClear();
    lcdPrintText(0,0, "connecting huskylens", 0xffff);

    while (!husky.connected()) {
        delay(200);
    }

    lcdClear();
    lcdPrintText(0,0, "connected", 0xffff);
    
    husky.algorithm(ALGORITHM_TAG_RECOGNITION);
    delay(500);
}

void loop() {
    std::vector<Block> blocks = husky.retrieve();
    
    lcdClear();
    if (blocks.empty()) {
        lcdPrintText(0,0,"Not found", 0xffff);
    } else {
        lcdPrintText(0,0, String(blocks[0].id), 0xffff);
        lcdPrintText(20,0, String(blocks[0].x), 0xffff);
        lcdPrintText(40,0, String(blocks[0].y), 0xffff);
    }
    
    delay(50);
}
