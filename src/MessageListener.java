package pipeipc;

/** Callback invoked when a message with a subscribed label arrives. */
@FunctionalInterface
public interface MessageListener {
    void onMessage(String label, Object value);
}
