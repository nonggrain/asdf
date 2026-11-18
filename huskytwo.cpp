// #include <POP32.h>
#include <Wire.h>
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

const uint8_t HUSKYLENS_I2C_ADDR = 0x32;
const uint8_t HEADER[] = {0x55, 0xAA, 0x11};
const uint8_t CMD_REQUEST_BLOCKS_BY_ID = 0x27;
const uint8_t CMD_REQUEST_LEARNED = 0x23;
const uint8_t CMD_RETURN_INFO = 0x29;
const uint8_t CMD_RETURN_BLOCK = 0x2A;
const uint8_t CMD_REQUEST_KNOCK = 0x2C;
const uint8_t CMD_REQUEST_ALGORITHM = 0x2D;
const uint8_t CMD_RETURN_OK = 0x2E;
const uint8_t CMD_REQUEST_FIRMWARE_VERSION = 0x3C;
const uint8_t CMD_REQUEST_CUSTOM_TEXT = 0x34;
const uint8_t CMD_REQUEST_CLEAR_TEXT = 0x35;

const int ALGORITHM_FACE_RECOGNITION = 0;
const int ALGORITHM_OBJECT_TRACKING = 1;
const int ALGORITHM_OBJECT_RECOGNITION = 2;
const int ALGORITHM_LINE_TRACKING = 3;
const int ALGORITHM_COLOR_RECOGNITION = 4;
const int ALGORITHM_TAG_RECOGNITION = 5;
const int ALGORITHM_OBJECT_CLASSIFICATION = 6;

class Block {
public:
    int x, y, width, height, id, confidence;
    std::string name, content;

    Block(int x, int y, int width, int height, int id, std::string name = "", std::string content = "", int confidence = 0)
        : x(x), y(y), width(width), height(height), id(id), confidence(confidence), name(name), content(content) {}

    int cx() const { return x + width / 2; }
    int cy() const { return y + height / 2; }
    int area() const { return width * height; }
    float ratio() const { return (height != 0) ? float(width) / height : 0.0f; }
};

class Huskylens {
private:
    TwoWire* wire;
    uint8_t i2c_addr;
    uint8_t cmd_buffer[256];

public:
    Huskylens(TwoWire* wire_bus = &Wire, uint8_t addr = HUSKYLENS_I2C_ADDR) 
        : wire(wire_bus), i2c_addr(addr) {}

    void begin() {
        wire->begin();
        wire->setClock(100000);
    }

    uint8_t checksum(const std::vector<uint8_t>& data) {
        uint32_t sum = 0;
        for (auto b : data) {
            sum += b;
        }
        return uint8_t(sum & 0xFF);
    }

    void flush() {
        while (wire->available()) {
            wire->read();
        }
    }

    std::vector<uint8_t> _readv1(int size) {
        std::vector<uint8_t> data;
        if (size <= 0) return data;

        uint8_t received = wire->requestFrom(i2c_addr, (uint8_t)size);
        for (int i = 0; i < received; ++i) {
            if (wire->available()) {
                data.emplace_back(uint8_t(wire->read()));
            }
        }
        return data;
    }

    struct huskyresponse {
        bool good;
        uint8_t command;
        std::vector<uint8_t> payload;
    };

    huskyresponse readv1() {
        std::vector<uint8_t> window;
        bool foundhead = false;
        
        for (int i = 0; i < 25; ++i) {
            std::vector<uint8_t> b = _readv1(1);
            if (b.empty()) {
                delay(1);
                continue;
            }

            window.emplace_back(b[0]);
            if (window.size() > 3) window.erase(window.begin());

            if (window.size() == 3 && window[0] == HEADER[0] && window[1] == HEADER[1] && window[2] == HEADER[2]) {
                foundhead = true;
                break;
            }
        }
        if (!foundhead) {
            return {false, 0, {}};
        }

        std::vector<uint8_t> meta = _readv1(2);
        if (meta.size() < 2) {
            return {false, 0, {}};
        }

        uint8_t length = meta[0];
        uint8_t command = meta[1];

        std::vector<uint8_t> body = _readv1(length + 1);
        if (body.size() < (size_t)(length + 1)) {
            return {false, 0, {}};
        }

        uint8_t received_checksum = body.back();
        body.pop_back();

        std::vector<uint8_t> expected = {HEADER[0], HEADER[1], HEADER[2], length, command};
        expected.insert(expected.end(), body.begin(), body.end());

        if (received_checksum == checksum(expected)) {
            return {true, command, body};
        } else {
            return {false, 0, {}};
        }
    }

    void cmdv1(int command, const std::vector<uint8_t>& payload = {}) {
        size_t payload_length = payload.size();
        size_t total_len = 6 + payload_length;
        if (total_len >= 256) return;

        cmd_buffer[0] = 0x55;
        cmd_buffer[1] = 0xAA;
        cmd_buffer[2] = 0x11;
        cmd_buffer[3] = uint8_t(payload_length);
        cmd_buffer[4] = command;

        for (size_t i = 0; i < payload_length; ++i) {
            cmd_buffer[5 + i] = payload[i];
        }

        std::vector<uint8_t> checksum_data(cmd_buffer, cmd_buffer + 5 + payload_length);
        cmd_buffer[5 + payload_length] = checksum(checksum_data);

        size_t offset = 0;
        while (offset < total_len) {
            size_t chunk = total_len - offset;
            if (chunk > 32) chunk = 32;

            wire->beginTransmission(i2c_addr);
            wire->write(cmd_buffer + offset, chunk);
            wire->endTransmission();
            
            offset += chunk;
        }
    }

    bool ok(int retries = 10) {
        for (int i = 0; i < retries; ++i) {
            huskyresponse res = readv1();
            if (res.good && res.command == CMD_RETURN_OK) return true;
            delay(10);
        }
        return false;
    }

    bool connected() {
        flush();
        cmdv1(CMD_REQUEST_KNOCK);
        return ok();
    }

    bool algorithm(int id) {
        int16_t val = id;
        std::vector<uint8_t> payload(sizeof(val));
        memcpy(payload.data(), &val, sizeof(val));
        cmdv1(CMD_REQUEST_ALGORITHM, payload);
        return ok();
    }

    std::vector<Block> retrieve(int id = -1) {
        std::vector<Block> blocks;
        if (id >= 0) {
            int16_t val = id;
            std::vector<uint8_t> payload(sizeof(val));
            memcpy(payload.data(), &val, sizeof(val));
            cmdv1(CMD_REQUEST_BLOCKS_BY_ID, payload);
        } else {
            cmdv1(CMD_REQUEST_LEARNED);
        }

        huskyresponse res = readv1();
        if (!res.good) return blocks;

        if (res.command == CMD_RETURN_INFO) {
            if (res.payload.size() < 2) return {};

            int16_t n = 0;
            memcpy(&n, res.payload.data(), sizeof(n));

            for (int i = 0; i < n; ++i) {
                huskyresponse res2 = readv1();
                if (res2.good && res2.command == CMD_RETURN_BLOCK && res2.payload.size() >= 10) {
                    int16_t fields[5];
                    memcpy(fields, res2.payload.data(), sizeof(fields));

                    Block obj{fields[0], fields[1], fields[2], fields[3], fields[4]};
                    if (id < 0 || obj.id == id) {
                        blocks.push_back(obj);
                    }
                }
            }
        }
        flush();
        return blocks;
    }
};
