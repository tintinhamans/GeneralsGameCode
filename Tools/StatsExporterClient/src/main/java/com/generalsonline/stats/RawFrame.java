package com.generalsonline.stats;

/** One undecoded frame: the type byte and the body. */
public record RawFrame(int type, byte[] body) {
}
