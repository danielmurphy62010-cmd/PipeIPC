import pipeipc.Client;
import pipeipc.Message;
import pipeipc.Server;
import pipeipc.SharedVars;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class JavaExample {
    private static void waitFor(java.util.function.BooleanSupplier condition, String what) throws Exception {
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (System.nanoTime() < end) {
            if (condition.getAsBoolean()) return;
            Thread.sleep(10);
        }
        throw new IllegalStateException("Timed out waiting for " + what);
    }

    public static void main(String[] args) throws Exception {
        try (Server server = new Server("java-example")) {
            server.start();
            try (Client first = new Client("java-example");
                 Client second = new Client("java-example")) {
                first.connect();
                second.connect();
                try (SharedVars firstVars = new SharedVars(first);
                     SharedVars secondVars = new SharedVars(second)) {
                    Map<String,Object> initial = new LinkedHashMap<>();
                    initial.put("numbers", java.util.List.of(1L, 2.5, true));
                    firstVars.set("state", initial);
                    waitFor(() -> secondVars.contains("state"), "shared-variable update");

                    secondVars.set("state", "written by the other client");
                    waitFor(() -> "written by the other client".equals(firstVars.get("state")),
                            "cross-client variable write");

                    CountDownLatch handled = new CountDownLatch(1);
                    second.on("example.message", (label, value) -> {
                        System.out.println("on(): " + label + " = " + value);
                        handled.countDown();
                    });
                    first.send("example.message", Map.of("text", "hello", "count", 3L));

                    Message message = second.recv(5000);
                    if (message == null) throw new IllegalStateException("recv() timed out");
                    if (!handled.await(5, TimeUnit.SECONDS))
                        throw new IllegalStateException("on() timed out");
                    System.out.println("recv(): " + message);
                }
            }
        }
    }
}
