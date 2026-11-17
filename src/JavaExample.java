import pipeipc.Client;
import pipeipc.Message;
import pipeipc.Server;
import pipeipc.SharedVars;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class JavaExample {
    private static final class AppState extends SharedVars {
        public volatile Object counter = 0L;
        public volatile Object config = new java.util.LinkedHashMap<String, Object>();
        public volatile Object status = "idle";

        AppState(Client client) {
            super(client);
            startSync();
        }
    }

    private static void waitFor(java.util.function.BooleanSupplier condition, String what) throws Exception {
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (System.nanoTime() < end) {
            if (condition.getAsBoolean()) return;
            Thread.sleep(10);
        }
        throw new IllegalStateException("Timed out waiting for " + what);
    }

    @SuppressWarnings("unchecked")
    public static void main(String[] args) throws Exception {
        try (Server server = new Server("java-example")) {
            server.start();
            try (Client first = new Client("java-example");
                 Client second = new Client("java-example")) {
                first.connect();
                second.connect();
                try (AppState firstVars = new AppState(first);
                     AppState secondVars = new AppState(second)) {
                    firstVars.status = "ready";
                    firstVars.counter = 1L;
                    ((Map<String, Object>) firstVars.config).put("enabled", true);
                    waitFor(() -> "ready".equals(secondVars.status)
                                    && ((Map<String, Object>) secondVars.config).containsKey("enabled"),
                            "shared field creation");

                    ((Map<String, Object>) firstVars.config).put("enabled", false);
                    firstVars.counter = 2L;
                    waitFor(() -> Long.valueOf(2).equals(secondVars.counter)
                                    && ((Map<String, Object>) secondVars.config).containsValue(Boolean.FALSE),
                            "nested and scalar field updates");

                    firstVars.status = SharedVars.DELETE;
                    waitFor(() -> secondVars.status == SharedVars.DELETE, "shared field deletion");

                    CountDownLatch handled = new CountDownLatch(1);
                    second.on("example.event", (label, value) -> {
                        System.out.println("on(): " + label + " = " + value);
                        handled.countDown();
                    });
                    first.send("example.event", Map.of("text", "handled by on"));
                    first.send("example.recv", Map.of("text", "received by recv"));
                    Message message = second.recv(5000);
                    if (message == null) throw new IllegalStateException("recv() timed out");
                    if (!handled.await(5, TimeUnit.SECONDS))
                        throw new IllegalStateException("on() timed out");
                    System.out.println("recv(): " + message);
                    System.out.println("Shared variables: counter=" + firstVars.counter
                            + ", status=" + firstVars.status + ", config=" + firstVars.config);
                }
            }
        }
    }
}
