package com.generalsonline.stats.app;

import com.generalsonline.stats.DecodedFrame;
import com.generalsonline.stats.FrameType;
import com.generalsonline.stats.proto.Events;
import com.generalsonline.stats.proto.StatsSnapshot;

import java.util.Objects;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Counts what the feed delivers so the footer can show whether it is healthy: light and full snapshots, events,
 * camera frames and everything else per second, and the time since the last frame. {@link #record} is thread safe
 * (the reader thread calls it); {@link #sample} is called by one thread, the window's once-a-second timer.
 */
final class FeedHealth {
    /** No frame for this long is shown as a stalled feed. */
    static final double STALE_SECONDS = 3.0;

    private final AtomicLong lightSnapshots = new AtomicLong();
    private final AtomicLong fullSnapshots = new AtomicLong();
    private final AtomicLong events = new AtomicLong();
    private final AtomicLong cameraFrames = new AtomicLong();
    private final AtomicLong otherFrames = new AtomicLong();
    private final AtomicLong bytes = new AtomicLong();
    private volatile long lastFrameNanos;
    private volatile boolean anyFrame;

    private long previousNanos;
    private long previousLight;
    private long previousFull;
    private long previousEvents;
    private long previousCamera;
    private long previousOther;
    private long previousBytes;

    /** @param nowNanos System.nanoTime() (a parameter so tests control time) */
    void record(DecodedFrame frame, long nowNanos) {
        bytes.addAndGet(frame.wireBytes());
        lastFrameNanos = nowNanos;
        anyFrame = true;
        if (frame.message() instanceof StatsSnapshot) {
            (((StatsSnapshot) frame.message()).getFull() ? fullSnapshots : lightSnapshots).incrementAndGet();
        } else if (frame.message() instanceof Events) {
            events.addAndGet(((Events) frame.message()).getEventsCount());
            otherFrames.incrementAndGet();
        } else if (frame.type() == FrameType.CAMERA) {
            cameraFrames.incrementAndGet();
        } else {
            otherFrames.incrementAndGet();
        }
    }

    long totalFrames() {
        return lightSnapshots.get() + fullSnapshots.get() + cameraFrames.get() + otherFrames.get();
    }

    long totalBytes() {
        return bytes.get();
    }

    long cameraFrames() {
        return cameraFrames.get();
    }

    /** Rates over the time since the previous sample. secondsSinceFrame is -1 before any frame arrived. */
    static final class Sample {
        private final double lightPerSecond;
        private final double fullPerSecond;
        private final double eventsPerSecond;
        private final double cameraHz;
        private final double otherPerSecond;
        private final double kilobytesPerSecond;
        private final double secondsSinceFrame;
        private final long totalFrames;
        private final long totalBytes;

        Sample(double lightPerSecond, double fullPerSecond, double eventsPerSecond, double cameraHz, double otherPerSecond, double kilobytesPerSecond, double secondsSinceFrame, long totalFrames, long totalBytes) {
            this.lightPerSecond = lightPerSecond;
            this.fullPerSecond = fullPerSecond;
            this.eventsPerSecond = eventsPerSecond;
            this.cameraHz = cameraHz;
            this.otherPerSecond = otherPerSecond;
            this.kilobytesPerSecond = kilobytesPerSecond;
            this.secondsSinceFrame = secondsSinceFrame;
            this.totalFrames = totalFrames;
            this.totalBytes = totalBytes;
        }

        double lightPerSecond() {
            return lightPerSecond;
        }

        double fullPerSecond() {
            return fullPerSecond;
        }

        double eventsPerSecond() {
            return eventsPerSecond;
        }

        double cameraHz() {
            return cameraHz;
        }

        double otherPerSecond() {
            return otherPerSecond;
        }

        double kilobytesPerSecond() {
            return kilobytesPerSecond;
        }

        double secondsSinceFrame() {
            return secondsSinceFrame;
        }

        long totalFrames() {
            return totalFrames;
        }

        long totalBytes() {
            return totalBytes;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof Sample)) {
                return false;
            }
            Sample other = (Sample) o;
            return Double.compare(lightPerSecond, other.lightPerSecond) == 0
                    && Double.compare(fullPerSecond, other.fullPerSecond) == 0
                    && Double.compare(eventsPerSecond, other.eventsPerSecond) == 0
                    && Double.compare(cameraHz, other.cameraHz) == 0
                    && Double.compare(otherPerSecond, other.otherPerSecond) == 0
                    && Double.compare(kilobytesPerSecond, other.kilobytesPerSecond) == 0
                    && Double.compare(secondsSinceFrame, other.secondsSinceFrame) == 0
                    && totalFrames == other.totalFrames
                    && totalBytes == other.totalBytes;
        }

        @Override
        public int hashCode() {
            return Objects.hash(lightPerSecond, fullPerSecond, eventsPerSecond, cameraHz, otherPerSecond, kilobytesPerSecond, secondsSinceFrame, totalFrames, totalBytes);
        }

        @Override
        public String toString() {
            return "Sample[" + "lightPerSecond=" + lightPerSecond + ", fullPerSecond=" + fullPerSecond + ", eventsPerSecond=" + eventsPerSecond + ", cameraHz=" + cameraHz + ", otherPerSecond=" + otherPerSecond + ", kilobytesPerSecond=" + kilobytesPerSecond + ", secondsSinceFrame=" + secondsSinceFrame + ", totalFrames=" + totalFrames + ", totalBytes=" + totalBytes + "]";
        }
    }

    Sample sample(long nowNanos) {
        double seconds = Math.max(0.001, (nowNanos - previousNanos) / 1e9);
        long light = lightSnapshots.get();
        long full = fullSnapshots.get();
        long eventCount = events.get();
        long camera = cameraFrames.get();
        long other = otherFrames.get();
        long byteCount = bytes.get();
        Sample sample = new Sample((light - previousLight) / seconds, (full - previousFull) / seconds, (eventCount - previousEvents) / seconds,
            (camera - previousCamera) / seconds, (other - previousOther) / seconds, (byteCount - previousBytes) / 1024.0 / seconds,
            anyFrame ? Math.max(0, (nowNanos - lastFrameNanos) / 1e9) : -1, totalFrames(), byteCount);
        previousNanos = nowNanos;
        previousLight = light;
        previousFull = full;
        previousEvents = eventCount;
        previousCamera = camera;
        previousOther = other;
        previousBytes = byteCount;
        return sample;
    }

    /** True when frames were arriving and then stopped for longer than {@link #STALE_SECONDS}. */
    static boolean isStale(Sample sample) {
        return sample.secondsSinceFrame() > STALE_SECONDS;
    }

    /** The feed as a quiet footer: dim names, muted values, one group, red once frames stop. */
    static String quiet(Sample s) {
        String dim = "<span style='color:#5E6B7C'>";
        String last = s.secondsSinceFrame() < 0 ? "no frame yet"
            : isStale(s) ? String.format("<span style='color:#F0626A'>no frames for %.0f s</span>", s.secondsSinceFrame())
            : String.format("last frame %.1f s ago", s.secondsSinceFrame());
        return String.format("<html>%sFeed</span>&nbsp; %.0f snapshots/s&nbsp;&nbsp; %.0f events/s&nbsp;&nbsp; camera %.0f Hz&nbsp;&nbsp; %.1f KB/s&nbsp;&nbsp; %s</html>",
            dim, s.lightPerSecond() + s.fullPerSecond(), s.eventsPerSecond(), s.cameraHz(), s.kilobytesPerSecond(), last);
    }

    /** "Feed: light 10.0/s  full 4.0/s  events 3/s  camera 118 Hz  other 1.0/s  12.3 KB/s    last frame 0.1 s ago ...". */
    static String text(Sample s) {
        String last = s.secondsSinceFrame() < 0 ? "no frame yet"
            : isStale(s) ? String.format("NO FRAMES for %.0f s", s.secondsSinceFrame())
            : String.format("last frame %.1f s ago", s.secondsSinceFrame());
        return String.format("Feed: light %.1f/s  full %.1f/s  events %.0f/s  camera %.0f Hz  other %.1f/s  %.1f KB/s    %s    (total %d frames, %.1f KB)",
            s.lightPerSecond(), s.fullPerSecond(), s.eventsPerSecond(), s.cameraHz(), s.otherPerSecond(), s.kilobytesPerSecond(), last,
            s.totalFrames(), s.totalBytes() / 1024.0);
    }
}
