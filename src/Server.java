package pipeipc;

/** Named-pipe message broker. Start one Server for a namespace before clients connect. */
public final class Server implements AutoCloseable {
    static { System.loadLibrary("pipeipc_jni"); }
    private long handle;

    public Server() { this("default"); }
    public Server(String name) {
        handle = nativeCreate(name);
        if (handle == 0) throw new IllegalStateException("Could not create PipeIPC server");
    }
    public synchronized void start() { ensureOpen(); nativeStart(handle); }
    public synchronized void stop() { if (handle != 0) nativeStop(handle); }
    public void publish(String label, byte[] payload) { ensureOpen(); nativePublish(handle, label, payload); }
    private void ensureOpen() { if (handle == 0) throw new IllegalStateException("Server is closed"); }
    @Override public synchronized void close() {
        if (handle != 0) { nativeClose(handle); handle = 0; }
    }
    private static native long nativeCreate(String name);
    private static native void nativeStart(long handle);
    private static native void nativeStop(long handle);
    private static native void nativePublish(long handle, String label, byte[] payload);
    private static native void nativeClose(long handle);
}
