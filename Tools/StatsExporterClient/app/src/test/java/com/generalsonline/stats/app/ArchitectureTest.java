package com.generalsonline.stats.app;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/** The app talks to the library only through its public API, and the examples do too. */
class ArchitectureTest {
    // Split so that this file does not match itself.
    private static final String INTERNAL = "com.generalsonline.stats." + "internal";

    private static List<Path> javaFiles(Path root) throws IOException {
        assertTrue(Files.isDirectory(root), "missing folder " + root.toAbsolutePath());
        try (Stream<Path> files = Files.walk(root)) {
            return files.filter(f -> f.toString().endsWith(".java")).collect(Collectors.toList());
        }
    }

    private static List<String> offenders(Path root) throws IOException {
        List<String> found = new ArrayList<>();
        for (Path file : javaFiles(root)) {
            try {
                if (Files.readString(file, StandardCharsets.UTF_8).contains(INTERNAL)) {
                    found.add(file.toString());
                }
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
        }
        return found;
    }

    @Test
    void theAppNeverImportsTheLibrarysInternalPackage() throws IOException {
        assertEquals(List.of(), offenders(Path.of("src/main/java")));
        assertEquals(List.of(), offenders(Path.of("src/test/java")));
    }

    @Test
    void theExamplesNeverImportItEither() throws IOException {
        assertEquals(List.of(), offenders(Path.of("../examples/src/main/java")));
    }

    @Test
    void theAppHasNoClassInTheLibrarysPackages() throws IOException {
        // Same package names in two jars would break on the module path; the app lives in its own package.
        for (Path file : javaFiles(Path.of("src/main/java"))) {
            String text = Files.readString(file, StandardCharsets.UTF_8);
            assertTrue(text.startsWith("package com.generalsonline.stats.app;"), file.toString());
        }
    }
}
