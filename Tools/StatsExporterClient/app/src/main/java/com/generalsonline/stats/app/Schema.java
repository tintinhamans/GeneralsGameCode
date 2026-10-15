package com.generalsonline.stats.app;

import com.generalsonline.stats.proto.StatsExporter;
import com.google.protobuf.Descriptors;

import java.io.PrintStream;

/** Prints the frame table and every message and enum of the proto, from the generated descriptors. */
public final class Schema {
    private Schema() {
    }

    public static void print(PrintStream out) {
        out.println("Frames from the game (u32 body length, u8 type, u32 sequence, u64 timestamp_us, body; little-endian, 17 byte header):");
        out.println("  0x09 Hello          out  the first frame of a connection: protocol version, game build, capabilities, limits");
        out.println("  0x01 MatchInfo      out  match loading/started/ended, players, lobby data");
        out.println("  0x02 StatsSnapshot  out  periodic live stats: light every 100 ms of game time, full every 250 ms (also a bare stall marker)");
        out.println("  0x03 MatchSummary   out  end-of-match scores");
        out.println("  0x04 ChatMessage    out  a chat line as shown to the local player");
        out.println("  0x05 UiState        out  the screen the game is on; first frame after the Hello");
        out.println("  0x06 LobbyState     out  the game setup lobby the user sits in");
        out.println("  0x07 Events         out  the discrete events of one logic frame");
        out.println("  0x08 Camera         out  the local camera and minimap view box, from the render loop when it changed");
        out.println("  0x0A Response       out  the answer to one Request, only to the reader that asked");
        out.println("Frames to the game (u32 body length, u8 type, body; 5 byte header):");
        out.println("  0x10 ChatSend       in   send a chat line through the game's typed-line path");
        out.println("  0x11 Request        in   a query (catalog, object, objects, player, map, resync, feed config)");
        out.println();

        Descriptors.FileDescriptor file = StatsExporter.getDescriptor();
        out.println("package " + file.getPackage());
        for (Descriptors.EnumDescriptor e : file.getEnumTypes()) {
            out.println();
            out.println("enum " + e.getName());
            for (Descriptors.EnumValueDescriptor v : e.getValues()) {
                out.println("  " + v.getNumber() + " " + v.getName());
            }
        }
        for (Descriptors.Descriptor m : file.getMessageTypes()) {
            out.println();
            out.println("message " + m.getName());
            for (Descriptors.FieldDescriptor f : m.getFields()) {
                String type = f.getJavaType() == Descriptors.FieldDescriptor.JavaType.MESSAGE ? f.getMessageType().getName()
                    : f.getJavaType() == Descriptors.FieldDescriptor.JavaType.ENUM ? f.getEnumType().getName()
                    : f.getType().name().toLowerCase();
                out.println("  " + f.getNumber() + " " + (f.isRepeated() ? "repeated " : "") + type + " " + f.getName());
            }
        }
    }
}
