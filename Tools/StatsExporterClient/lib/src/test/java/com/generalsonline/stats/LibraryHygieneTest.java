package com.generalsonline.stats;

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
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/** The library keeps to its brief: no Swing, no CLI, no process exit, no printing, and only two hand-written packages. */
class LibraryHygieneTest {
    private static final Path SOURCES = Path.of("src/main/java");

    private static final List<String> FORBIDDEN = List.of("javax.swing", "java.awt", "System.exit", "System.out", "System.err", "printStackTrace", "Runtime.getRuntime().halt");

    private static List<Path> javaFiles() throws IOException {
        assertTrue(Files.isDirectory(SOURCES), "run from the lib project folder: " + SOURCES.toAbsolutePath());
        try (Stream<Path> files = Files.walk(SOURCES)) {
            return files.filter(f -> f.toString().endsWith(".java")).collect(Collectors.toList());
        }
    }

    /** The code without block comments and line comments, so that documentation may mention System.out. */
    private static String code(Path file) {
        try {
            String text = Files.readString(file, StandardCharsets.UTF_8);
            text = Pattern.compile("/\\*.*?\\*/", Pattern.DOTALL).matcher(text).replaceAll("");
            return Pattern.compile("//[^\n]*").matcher(text).replaceAll("");
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    @Test
    void noSwingNoExitNoPrinting() throws IOException {
        List<String> offenders = new ArrayList<>();
        for (Path file : javaFiles()) {
            String code = code(file);
            for (String token : FORBIDDEN) {
                if (code.contains(token)) {
                    offenders.add(file.getFileName() + " uses " + token);
                }
            }
        }
        assertEquals(List.of(), offenders);
    }

    @Test
    void theHandWrittenCodeIsInTheApiPackageAndTheInternalOne() throws IOException {
        List<String> wrong = new ArrayList<>();
        for (Path file : javaFiles()) {
            String relative = SOURCES.relativize(file).getParent().toString().replace('\\', '/');
            if (!relative.equals("com/generalsonline/stats") && !relative.equals("com/generalsonline/stats/internal")) {
                wrong.add(file.toString());
            }
        }
        assertEquals(List.of(), wrong);
    }

    @Test
    void thePublicApiNeverMentionsTheInternalPackage() throws IOException {
        // Internal types may be used inside method bodies and package-private members, never in a public signature.
        String internal = "com.generalsonline.stats." + "internal";
        Pattern publicSignature = Pattern.compile("(?m)^\\s*public [^=;{]*\\b(FrameCodec|PipeLink|NamedPipeLink|ReplayPacer)\\b");
        List<String> offenders = new ArrayList<>();
        try (Stream<Path> files = Files.walk(SOURCES.resolve("com/generalsonline/stats"), 1)) {
            for (Path file : (Iterable<Path>) files.filter(f -> f.toString().endsWith(".java"))::iterator) {
                String code = code(file);
                if (publicSignature.matcher(code).find()) {
                    offenders.add(file.getFileName().toString());
                }
                assertTrue(!code.contains("import static " + internal), file.toString());
            }
        }
        assertEquals(List.of(), offenders);
    }
}
