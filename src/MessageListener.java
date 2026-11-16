package pipeipc;

/** Receives a labeled message delivered to a PipeIPC client. */
@FunctionalInterface
public interface MessageListener {
    void onMessage(String label, byte[] payload);
}
