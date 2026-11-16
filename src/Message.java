package pipeipc;

/** A message returned by Client.recv(). */
public final class Message {
    private final String label;
    private final Object value;

    public Message(String label, Object value) {
        this.label = label;
        this.value = value;
    }
    public String label() { return label; }
    public Object value() { return value; }
    @Override public String toString() { return "Message[label=" + label + ", value=" + value + "]"; }
}
