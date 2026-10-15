package com.generalsonline.stats;

/**
 * One undecoded frame: the type byte and the protobuf body, without the length prefix.
 *
 * @param type the type byte of the frame, 0 to 255
 * @param body the protobuf body; the array is not copied, so do not modify it
 */
public record RawFrame(int type, byte[] body) {
}
