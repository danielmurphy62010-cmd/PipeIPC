package quickipc;
import java.util.Map;
public final class SharedVars implements AutoCloseable {
    static { System.loadLibrary("quickipc_jni"); }
    private long handle;
    public SharedVars(String label, String processId, boolean creator) {
        handle = nativeCreate(label, processId, creator);
        if (handle == 0) throw new IllegalStateException("Could not create SharedVars");
    }
    public SharedVars(String label) { this(label, "java", true); }
    public synchronized void set(String key, Object value) { ensureOpen(); nativeSet(handle, key, value); }
    public synchronized Object get(String key) { ensureOpen(); return nativeGet(handle, key); }
    public synchronized Map<String,Object> snapshot() { ensureOpen(); return nativeSnapshot(handle); }
    private void ensureOpen() { if (handle == 0) throw new IllegalStateException("SharedVars is closed"); }
    @Override public synchronized void close() { if (handle != 0) { nativeClose(handle); handle = 0; } }
    private static native long nativeCreate(String label, String processId, boolean creator);
    private static native void nativeSet(long handle, String key, Object value);
    private static native Object nativeGet(long handle, String key);
    private static native Map<String,Object> nativeSnapshot(long handle);
    private static native void nativeClose(long handle);
}
