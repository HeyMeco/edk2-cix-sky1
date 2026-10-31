// SPDX-License-Identifier: BSD-2-Clause-Patent
// Ghidra headless post-script: apply "address name" pairs (funcmatch.py
// output), create and analyse those functions, then decompile every
// function into <out>.c, unnamed ones (code the reference tree lacks)
// first into <out>-unmatched.c.
//
//   analyzeHeadless DIR PROJ -import IMG -processor AARCH64:LE:64:v8A \
//     -loader BinaryLoader -loader-baseAddr BASE \
//     -postScript NameAndDecompile.java NAMES.txt OUT
//@category CIX

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class NameAndDecompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Set<Address> named = new HashSet<>();
        for (String line : Files.readAllLines(Paths.get(args[0]))) {
            String[] p = line.trim().split("\\s+");
            if (p.length < 2 || p[0].startsWith("#")) continue;
            Address a = toAddr(Long.decode(p[0]));
            disassemble(a);
            Function f = getFunctionAt(a);
            if (f == null) f = createFunction(a, p[1]);
            if (f != null) { f.setName(p[1], SourceType.IMPORTED); named.add(a); }
        }
        analyzeAll(currentProgram);
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter all = new PrintWriter(args[1] + ".c");
             PrintWriter un = new PrintWriter(args[1] + "-unmatched.c")) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                DecompileResults r = d.decompileFunction(f, 60, monitor);
                String c = r.decompileCompleted() ? r.getDecompiledFunction().getC() : "/* decompile failed */\n";
                String hdr = "// " + f.getEntryPoint() + " " + f.getName() + " size " + f.getBody().getNumAddresses() + "\n";
                all.print(hdr + c + "\n");
                if (!named.contains(f.getEntryPoint())) un.print(hdr + c + "\n");
            }
        }
    }
}
