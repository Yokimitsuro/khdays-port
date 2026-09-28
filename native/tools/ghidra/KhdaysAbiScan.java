// What the ARM code really passes and returns where the decompilation's C
// relies on registers (see native/tools/check_prototypes.py). Read-only: it
// changes nothing in the program.
//
// Input  build/native/gen/ghidra_targets.txt (native/tools/ghidra_abi_targets.py)
// Output build/native/gen/ghidra_abi.txt, one line per finding:
//   A <function> <return address> r0 <- <source> [| <source> ...]
//   B <caller> <callee> <call address> r<k> <- <source> [| ...]
// A source is the instruction that last wrote the register on that path
// (`call <name>` when a call left it), `entry:rK` when the register still
// holds the function's own argument, or `tail <instruction>` for a return
// through a jump to another function.
//@category khdays
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.block.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.FlowType;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

public class KhdaysAbiScan extends GhidraScript {
    static final String GEN = "E:/KH 3582/khdays-port/build/native/gen/";
    SimpleBlockModel model;
    StringBuilder out = new StringBuilder();

    Address addr(String space, String hex) {
        long offset = Long.parseLong(hex, 16);
        AddressSpace sp = space.equals("-")
            ? currentProgram.getAddressFactory().getDefaultAddressSpace()
            : currentProgram.getAddressFactory().getAddressSpace(space);
        return sp == null ? null : sp.getAddress(offset);
    }

    boolean writes(Instruction in, String reg) {
        if (in.getFlowType().isCall() && reg.matches("r[0-3]")) {
            return true;  // a call leaves r0-r3 to its callee
        }
        for (Object o : in.getResultObjects()) {
            if (o instanceof Register && ((Register) o).getName().equalsIgnoreCase(reg)) {
                return true;
            }
        }
        return false;
    }

    String describe(Instruction in) throws Exception {
        String s = in.getAddress() + " " + in;
        if (in.getMnemonicString().toLowerCase().startsWith("ldr")) {
            // a literal-pool load: the word it loads, and what it names
            for (ghidra.program.model.symbol.Reference r : in.getReferencesFrom()) {
                if (r.getReferenceType().isData() && r.getToAddress().isMemoryAddress()) {
                    long word = getInt(r.getToAddress()) & 0xffffffffL;
                    ghidra.program.model.symbol.Symbol sym =
                        getSymbolAt(r.getToAddress().getNewAddress(word));
                    s += " = 0x" + Long.toHexString(word) + (sym != null ? " " + sym.getName() : "");
                }
            }
        }
        if (in.getFlowType().isCall() || in.getFlowType().isJump()) {
            for (Address t : in.getFlows()) {
                Function g = getFunctionAt(t);
                if (g != null) {
                    s += " => 0x" + Long.toHexString(t.getOffset()) + " " + g.getName();
                }
            }
        }
        return s;
    }

    Instruction previousInBlock(Instruction in, CodeBlock block) {
        Instruction p = in.getPrevious();
        return p != null && block.contains(p.getAddress()) ? p : null;
    }

    void search(Function f, Instruction start, boolean inclusive, String reg, Set<String> found,
                Set<Address> visited, int depth) throws Exception {
        CodeBlock block = model.getFirstCodeBlockContaining(start.getAddress(), monitor);
        Instruction in = inclusive ? start : previousInBlock(start, block);
        while (in != null) {
            if (writes(in, reg)) {
                found.add(describe(in));
                return;
            }
            in = previousInBlock(in, block);
        }
        if (block.getFirstStartAddress().equals(f.getEntryPoint())) {
            found.add("entry:" + reg);
            return;
        }
        if (depth > 64) {
            found.add("too-deep");
            return;
        }
        boolean any = false;
        CodeBlockReferenceIterator it = block.getSources(monitor);
        while (it.hasNext()) {
            CodeBlockReference r = it.next();
            if (r.getFlowType().isCall()) {
                continue;
            }
            Address from = r.getReferent();
            if (!f.getBody().contains(from) || !visited.add(from)) {
                continue;
            }
            Instruction si = getInstructionAt(from);
            if (si != null) {
                any = true;
                search(f, si, true, reg, found, visited, depth + 1);
            }
        }
        if (!any && found.isEmpty()) {
            found.add("no-path");
        }
    }

    void returns(String name, Address a) throws Exception {
        Function f = a == null ? null : getFunctionAt(a);
        if (f == null) {
            out.append("A " + name + " - no-function\n");
            return;
        }
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction in = it.next();
            FlowType ft = in.getFlowType();
            boolean tail = false;
            if (ft.isJump()) {
                Address[] flows = in.getFlows();
                tail = (flows.length == 1 && !f.getBody().contains(flows[0]))
                    || (ft.isComputed() && !in.toString().matches("(?i)bx\\w*\\s+lr"));
            }
            if (tail) {
                out.append("A " + name + " " + in.getAddress() + " r0 <- tail " + describe(in) + "\n");
            } else if (ft.isTerminal()) {
                Set<String> found = new LinkedHashSet<>();
                search(f, in, false, "r0", found, new HashSet<>(), 0);
                out.append("A " + name + " " + in.getAddress() + " r0 <- " + String.join(" | ", found) + "\n");
            }
        }
    }

    void calls(String caller, Address ca, String callee, Address fa, int declared, int defined)
            throws Exception {
        Function f = ca == null ? null : getFunctionAt(ca);
        if (f == null || fa == null) {
            out.append("B " + caller + " " + callee + " - no-function\n");
            return;
        }
        boolean seen = false;
        InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
        while (it.hasNext()) {
            Instruction in = it.next();
            if (!(in.getFlowType().isCall() || in.getFlowType().isJump())) {
                continue;
            }
            if (!Arrays.asList(in.getFlows()).contains(fa)) {
                continue;
            }
            seen = true;
            for (int k = declared; k < defined; ++k) {
                if (k >= 4) {
                    out.append("B " + caller + " " + callee + " " + in.getAddress() + " arg" + k + " <- stack\n");
                    continue;
                }
                Set<String> found = new LinkedHashSet<>();
                search(f, in, false, "r" + k, found, new HashSet<>(), 0);
                out.append("B " + caller + " " + callee + " " + in.getAddress() + " r" + k + " <- "
                    + String.join(" | ", found) + "\n");
            }
        }
        if (!seen) {
            out.append("B " + caller + " " + callee + " - no-call-found\n");
        }
    }

    @Override
    protected void run() throws Exception {
        model = new SimpleBlockModel(currentProgram);
        for (String line : Files.readAllLines(Paths.get(GEN + "ghidra_targets.txt"))) {
            String[] w = line.trim().split(" ");
            if (w[0].equals("A")) {
                returns(w[1], addr(w[2], w[3]));
            } else if (w[0].equals("B")) {
                calls(w[1], addr(w[2], w[3]), w[4], addr(w[5], w[6]),
                      Integer.parseInt(w[7]), Integer.parseInt(w[8]));
            }
        }
        Files.write(Paths.get(GEN + "ghidra_abi.txt"), out.toString().getBytes(StandardCharsets.UTF_8));
    }
}
