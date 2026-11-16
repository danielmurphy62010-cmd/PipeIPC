import quickipc.SharedVars;
import java.util.*;

public final class JavaExample {
    public static void main(String[] args) {
        try (SharedVars state = new SharedVars("example")) {
            Map<String,Object> nested = new LinkedHashMap<>();
            nested.put("numbers", Arrays.asList(1L, 2.5, true));
            nested.put("coords", Arrays.asList(10L, 20L));
            nested.put("tags", new LinkedHashSet<>(Arrays.asList("ipc", "demo")));
            state.set("message", "hello");
            state.set("nested", nested);
            System.out.println(state.get("message"));
            System.out.println(state.snapshot());
        }
    }
}
