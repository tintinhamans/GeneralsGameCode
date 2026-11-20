package com.generalsonline.stats.app;

import java.util.prefs.Preferences;

/** The app's preferences, kept across runs by {@link Preferences} (the registry on Windows). Every call survives a store that cannot be read or written. */
final class Prefs {
    private static final String NODE = "/com/generalsonline/stats/client";

    private Prefs() {
    }

    private static Preferences node() {
        return Preferences.userRoot().node(NODE);
    }

    static String get(String key, String fallback) {
        try {
            return node().get(key, fallback);
        } catch (RuntimeException e) {
            return fallback;
        }
    }

    static boolean getBoolean(String key, boolean fallback) {
        try {
            return node().getBoolean(key, fallback);
        } catch (RuntimeException e) {
            return fallback;
        }
    }

    static void put(String key, String value) {
        try {
            if (value == null || value.isEmpty()) {
                node().remove(key);
            } else {
                node().put(key, value);
            }
            node().flush();
        } catch (Exception e) {
            // not being able to remember a setting must never break the window
        }
    }

    static void putBoolean(String key, boolean value) {
        put(key, Boolean.toString(value));
    }
}
