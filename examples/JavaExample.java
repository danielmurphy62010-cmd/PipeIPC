import quickipc.SharedVars;
import java.util.*;

public final class JavaExample {
    public static final class State extends SharedVars {
        public Object message;
        public Object nested;
        public State(String label) { super(label); }
    }
    public static void main(String[] args) {
        try (State state = new State("example")) {
            Map<String,Object> nested = new LinkedHashMap<>();
            nested.put("numbers", Arrays.asList(1L, 2.5, true));
            nested.put("coords", Arrays.asList(10L, 20L));
            nested.put("tags", new LinkedHashSet<>(Arrays.asList("ipc", "demo")));
            state.message = "hello";
            state.nested = nested;
            System.out.println(state.message);
            System.out.println(state.nested);
        }
    }
}
