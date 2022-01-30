var cursor = 0;
var buffer;

// Uncomment for ChirpStack:
function Decode(port, bytes, variables) {

    // Uncomment for The Things Network:
    // function Decoder(port, bytes) {

    buffer = bytes;

    if (bytes.length != 6) {
        return {};
    }

    var header = u8();
    var voltage = u8() / 10;
    var distance = s16() / 10;
    var temperature_core = s16() / 10;

    return {
        header: header,
        voltage: voltage,
        distance: distance,
        temperature_core: temperature_core
    };
}

function u8() {
    var value = buffer.slice(cursor);
    value = value[0];
    cursor = cursor + 1;
    return value;
}

function s16() {
    var value = buffer.slice(cursor);
    value = value[1] | value[0] << 8;
    if ((value & (1 << 15)) > 0) {
        value = (~value & 0xffff) + 1;
        value = -value;
    }
    cursor = cursor + 2;
    return value;
}
