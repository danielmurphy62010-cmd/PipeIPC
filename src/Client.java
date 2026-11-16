package pipeipc;

/** A connected process endpoint. Messages use send, recv, and on. */
public final class Client implements AutoCloseable {
    static { System.loadLibrary("pipeipc_jni"); }
    private long handle;

    public Client() { this("default"); }
    public Client(String name) {
        handle = nativeCreate(name);
        if (handle == 0) throw new IllegalStateException("Could not create PipeIPC client");
    }
    public synchronized void connect() { ensureOpen(); nativeConnect(handle); }
    public synchronized void disconnect() { if (handle != 0) nativeDisconnect(handle); }
    public boolean isConnected() { ensureOpen(); return nativeConnected(handle); }

    public void send(String label, Object value) { ensureOpen(); nativeSend(handle, label, value); }
    public Message recv() { return recv(-1); }
    /** Waits up to timeoutMillis; a negative timeout waits until a message arrives or disconnects. */
    public Message recv(long timeoutMillis) { ensureOpen(); return nativeRecv(handle, timeoutMillis); }
    public void on(String label, MessageListener listener) {
        ensureOpen();
        if (listener == null) throw new NullPointerException("listener");
        nativeOn(handle, label, listener);
    }
    public void off(String label) { ensureOpen(); nativeOff(handle, label); }

    long nativeHandle() { ensureOpen(); return handle; }
    private void ensureOpen() { if (handle == 0) throw new IllegalStateException("Client is closed"); }
    @Override public synchronized void close() {
        if (handle != 0) { nativeDisconnect(handle); nativeClose(handle); handle = 0; }
    }
    private static native long nativeCreate(String name);
    private static native void nativeConnect(long handle);
    private static native void nativeDisconnect(long handle);
    private static native boolean nativeConnected(long handle);
    private static native void nativeSend(long handle, String label, Object value);
    private static native Message nativeRecv(long handle, long timeoutMillis);
    private static native void nativeOn(long handle, String label, MessageListener listener);
    private static native void nativeOff(long handle, String label);
    private static native void nativeClose(long handle);
}
