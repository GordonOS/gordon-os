// ==========================================
//  gen-fixups.asm - stage 2 of fixup generation
// ==========================================
//  GENERATED FILE - do not edit by hand.
//  Source of truth: tools/tasks.json
//  Regenerate:     tools/gen-task-asm.ps1
//
//  Reads the base/shift/check temp assemblies produced by
//  assemble-tasks.asm and emits the final task binaries:
//
//    header + zero-pad to 256 + code body + fixup words + $FFFF
//
//  Fixup rule: every code byte where shift[i] == base[i] + 1 is an
//  absolute high byte, i.e. a relocation site. Offsets are
//  code-body-relative; the loader adds the page delta to each.
//
//    java -cp KickAss.jar kickass.KickAssembler -afo tools/gen-fixups.asm
//
//  Run from the project root, AFTER all three assemble-tasks runs.
// ==========================================

// --- basic - program storage is the REU working file (no pool RAM block); footprint 109/109 -- code 64 + eager gfx/FP + startup-lazy filesys.lib, see tools/check-pool-budget.ps1 ---
.var basicBase     = LoadBinary("../tasks-out/temp/basic.base.bin")
.var basicShift    = LoadBinary("../tasks-out/temp/basic.shift.bin")
.var basicCheck    = LoadBinary("../tasks-out/temp/basic.check.bin")
.var basicNameLen  = basicBase.get(12)
.var basicCodeSize = basicBase.getSize() - 256
.var basicFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < basicBase.getSize(); i++) {
    .if (((basicShift.get(i) & $FF) - (basicBase.get(i) & $FF)) == 1) {
        .eval basicFixups.add(i - 256)
    }
}
.print "basic: " + basicCodeSize + " bytes of code, " + basicFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < basicFixups.size(); f++) {
    .var idx = 256 + basicFixups.get(f)
    .if ((((basicBase.get(idx) & $FF) + 2) & $FF) != (basicCheck.get(idx) & $FF)) {
        .error "basic: fixup verify failed at code offset " + basicFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var basicFi = 0
.for (var i = 256; i < basicBase.getSize(); i++) {
    .if ((basicFi < basicFixups.size()) && (i - 256 == basicFixups.get(basicFi))) {
        .eval basicFi++
    } else {
        .if ((basicBase.get(i) & $FF) != (basicCheck.get(i) & $FF)) {
            .error "basic: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "basic: relocation verification OK"

.segmentdef basicFinal [outBin="../tasks-out/basic.bin"]
.segment basicFinal

// magic byte 1 (+0) is shared.
.fill 2, basicBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + basicNameLen, basicBase.get(2 + i)      // flags..name
    .word basicFixups.size()                          // fixup count (16-bit)
    .word 256 + basicCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + basicNameLen > 256) {
        .error "basic: header overflow"
    }
    .fill 256 - (17 + basicNameLen), 0                // pad to the 256-byte page
    .fill basicCodeSize, basicBase.get(256 + i)         // code body
    .for (var f = 0; f < basicFixups.size(); f++) {
        .word basicFixups.get(f)
    }
    .word $FFFF

// --- maze - re-runnable ---
.var mazeBase     = LoadBinary("../tasks-out/temp/maze.base.bin")
.var mazeShift    = LoadBinary("../tasks-out/temp/maze.shift.bin")
.var mazeCheck    = LoadBinary("../tasks-out/temp/maze.check.bin")
.var mazeNameLen  = mazeBase.get(12)
.var mazeCodeSize = mazeBase.getSize() - 256
.var mazeFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < mazeBase.getSize(); i++) {
    .if (((mazeShift.get(i) & $FF) - (mazeBase.get(i) & $FF)) == 1) {
        .eval mazeFixups.add(i - 256)
    }
}
.print "maze: " + mazeCodeSize + " bytes of code, " + mazeFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < mazeFixups.size(); f++) {
    .var idx = 256 + mazeFixups.get(f)
    .if ((((mazeBase.get(idx) & $FF) + 2) & $FF) != (mazeCheck.get(idx) & $FF)) {
        .error "maze: fixup verify failed at code offset " + mazeFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var mazeFi = 0
.for (var i = 256; i < mazeBase.getSize(); i++) {
    .if ((mazeFi < mazeFixups.size()) && (i - 256 == mazeFixups.get(mazeFi))) {
        .eval mazeFi++
    } else {
        .if ((mazeBase.get(i) & $FF) != (mazeCheck.get(i) & $FF)) {
            .error "maze: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "maze: relocation verification OK"

.segmentdef mazeFinal [outBin="../tasks-out/maze.bin"]
.segment mazeFinal

// magic byte 1 (+0) is shared.
.fill 2, mazeBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + mazeNameLen, mazeBase.get(2 + i)      // flags..name
    .word mazeFixups.size()                          // fixup count (16-bit)
    .word 256 + mazeCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + mazeNameLen > 256) {
        .error "maze: header overflow"
    }
    .fill 256 - (17 + mazeNameLen), 0                // pad to the 256-byte page
    .fill mazeCodeSize, mazeBase.get(256 + i)         // code body
    .for (var f = 0; f < mazeFixups.size(); f++) {
        .word mazeFixups.get(f)
    }
    .word $FFFF

// --- clock ---
.var clockBase     = LoadBinary("../tasks-out/temp/clock.base.bin")
.var clockShift    = LoadBinary("../tasks-out/temp/clock.shift.bin")
.var clockCheck    = LoadBinary("../tasks-out/temp/clock.check.bin")
.var clockNameLen  = clockBase.get(12)
.var clockCodeSize = clockBase.getSize() - 256
.var clockFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < clockBase.getSize(); i++) {
    .if (((clockShift.get(i) & $FF) - (clockBase.get(i) & $FF)) == 1) {
        .eval clockFixups.add(i - 256)
    }
}
.print "clock: " + clockCodeSize + " bytes of code, " + clockFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < clockFixups.size(); f++) {
    .var idx = 256 + clockFixups.get(f)
    .if ((((clockBase.get(idx) & $FF) + 2) & $FF) != (clockCheck.get(idx) & $FF)) {
        .error "clock: fixup verify failed at code offset " + clockFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var clockFi = 0
.for (var i = 256; i < clockBase.getSize(); i++) {
    .if ((clockFi < clockFixups.size()) && (i - 256 == clockFixups.get(clockFi))) {
        .eval clockFi++
    } else {
        .if ((clockBase.get(i) & $FF) != (clockCheck.get(i) & $FF)) {
            .error "clock: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "clock: relocation verification OK"

.segmentdef clockFinal [outBin="../tasks-out/clock.bin"]
.segment clockFinal

// magic byte 1 (+0) is shared.
.fill 2, clockBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + clockNameLen, clockBase.get(2 + i)      // flags..name
    .word clockFixups.size()                          // fixup count (16-bit)
    .word 256 + clockCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + clockNameLen > 256) {
        .error "clock: header overflow"
    }
    .fill 256 - (17 + clockNameLen), 0                // pad to the 256-byte page
    .fill clockCodeSize, clockBase.get(256 + i)         // code body
    .for (var f = 0; f < clockFixups.size(); f++) {
        .word clockFixups.get(f)
    }
    .word $FFFF

// --- gfxdemo - declares gfx.lib ---
.var gfxdemoBase     = LoadBinary("../tasks-out/temp/gfxdemo.base.bin")
.var gfxdemoShift    = LoadBinary("../tasks-out/temp/gfxdemo.shift.bin")
.var gfxdemoCheck    = LoadBinary("../tasks-out/temp/gfxdemo.check.bin")
.var gfxdemoNameLen  = gfxdemoBase.get(12)
.var gfxdemoCodeSize = gfxdemoBase.getSize() - 256
.var gfxdemoFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < gfxdemoBase.getSize(); i++) {
    .if (((gfxdemoShift.get(i) & $FF) - (gfxdemoBase.get(i) & $FF)) == 1) {
        .eval gfxdemoFixups.add(i - 256)
    }
}
.print "gfxdemo: " + gfxdemoCodeSize + " bytes of code, " + gfxdemoFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < gfxdemoFixups.size(); f++) {
    .var idx = 256 + gfxdemoFixups.get(f)
    .if ((((gfxdemoBase.get(idx) & $FF) + 2) & $FF) != (gfxdemoCheck.get(idx) & $FF)) {
        .error "gfxdemo: fixup verify failed at code offset " + gfxdemoFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var gfxdemoFi = 0
.for (var i = 256; i < gfxdemoBase.getSize(); i++) {
    .if ((gfxdemoFi < gfxdemoFixups.size()) && (i - 256 == gfxdemoFixups.get(gfxdemoFi))) {
        .eval gfxdemoFi++
    } else {
        .if ((gfxdemoBase.get(i) & $FF) != (gfxdemoCheck.get(i) & $FF)) {
            .error "gfxdemo: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "gfxdemo: relocation verification OK"

.segmentdef gfxdemoFinal [outBin="../tasks-out/gfxdemo.bin"]
.segment gfxdemoFinal

// magic byte 1 (+0) is shared.
.fill 2, gfxdemoBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + gfxdemoNameLen, gfxdemoBase.get(2 + i)      // flags..name
    .word gfxdemoFixups.size()                          // fixup count (16-bit)
    .word 256 + gfxdemoCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + gfxdemoNameLen > 256) {
        .error "gfxdemo: header overflow"
    }
    .fill 256 - (17 + gfxdemoNameLen), 0                // pad to the 256-byte page
    .fill gfxdemoCodeSize, gfxdemoBase.get(256 + i)         // code body
    .for (var f = 0; f < gfxdemoFixups.size(); f++) {
        .word gfxdemoFixups.get(f)
    }
    .word $FFFF

// --- fpdemo - declares fp.lib ---
.var fpdemoBase     = LoadBinary("../tasks-out/temp/fpdemo.base.bin")
.var fpdemoShift    = LoadBinary("../tasks-out/temp/fpdemo.shift.bin")
.var fpdemoCheck    = LoadBinary("../tasks-out/temp/fpdemo.check.bin")
.var fpdemoNameLen  = fpdemoBase.get(12)
.var fpdemoCodeSize = fpdemoBase.getSize() - 256
.var fpdemoFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < fpdemoBase.getSize(); i++) {
    .if (((fpdemoShift.get(i) & $FF) - (fpdemoBase.get(i) & $FF)) == 1) {
        .eval fpdemoFixups.add(i - 256)
    }
}
.print "fpdemo: " + fpdemoCodeSize + " bytes of code, " + fpdemoFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < fpdemoFixups.size(); f++) {
    .var idx = 256 + fpdemoFixups.get(f)
    .if ((((fpdemoBase.get(idx) & $FF) + 2) & $FF) != (fpdemoCheck.get(idx) & $FF)) {
        .error "fpdemo: fixup verify failed at code offset " + fpdemoFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var fpdemoFi = 0
.for (var i = 256; i < fpdemoBase.getSize(); i++) {
    .if ((fpdemoFi < fpdemoFixups.size()) && (i - 256 == fpdemoFixups.get(fpdemoFi))) {
        .eval fpdemoFi++
    } else {
        .if ((fpdemoBase.get(i) & $FF) != (fpdemoCheck.get(i) & $FF)) {
            .error "fpdemo: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "fpdemo: relocation verification OK"

.segmentdef fpdemoFinal [outBin="../tasks-out/fpdemo.bin"]
.segment fpdemoFinal

// magic byte 1 (+0) is shared.
.fill 2, fpdemoBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + fpdemoNameLen, fpdemoBase.get(2 + i)      // flags..name
    .word fpdemoFixups.size()                          // fixup count (16-bit)
    .word 256 + fpdemoCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + fpdemoNameLen > 256) {
        .error "fpdemo: header overflow"
    }
    .fill 256 - (17 + fpdemoNameLen), 0                // pad to the 256-byte page
    .fill fpdemoCodeSize, fpdemoBase.get(256 + i)         // code body
    .for (var f = 0; f < fpdemoFixups.size(); f++) {
        .word fpdemoFixups.get(f)
    }
    .word $FFFF

// --- spirog - integer spirograph; takes args R r d col ---
.var spirogBase     = LoadBinary("../tasks-out/temp/spirog.base.bin")
.var spirogShift    = LoadBinary("../tasks-out/temp/spirog.shift.bin")
.var spirogCheck    = LoadBinary("../tasks-out/temp/spirog.check.bin")
.var spirogNameLen  = spirogBase.get(12)
.var spirogCodeSize = spirogBase.getSize() - 256
.var spirogFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < spirogBase.getSize(); i++) {
    .if (((spirogShift.get(i) & $FF) - (spirogBase.get(i) & $FF)) == 1) {
        .eval spirogFixups.add(i - 256)
    }
}
.print "spirog: " + spirogCodeSize + " bytes of code, " + spirogFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < spirogFixups.size(); f++) {
    .var idx = 256 + spirogFixups.get(f)
    .if ((((spirogBase.get(idx) & $FF) + 2) & $FF) != (spirogCheck.get(idx) & $FF)) {
        .error "spirog: fixup verify failed at code offset " + spirogFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var spirogFi = 0
.for (var i = 256; i < spirogBase.getSize(); i++) {
    .if ((spirogFi < spirogFixups.size()) && (i - 256 == spirogFixups.get(spirogFi))) {
        .eval spirogFi++
    } else {
        .if ((spirogBase.get(i) & $FF) != (spirogCheck.get(i) & $FF)) {
            .error "spirog: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "spirog: relocation verification OK"

.segmentdef spirogFinal [outBin="../tasks-out/spirog.bin"]
.segment spirogFinal

// magic byte 1 (+0) is shared.
.fill 2, spirogBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + spirogNameLen, spirogBase.get(2 + i)      // flags..name
    .word spirogFixups.size()                          // fixup count (16-bit)
    .word 256 + spirogCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + spirogNameLen > 256) {
        .error "spirog: header overflow"
    }
    .fill 256 - (17 + spirogNameLen), 0                // pad to the 256-byte page
    .fill spirogCodeSize, spirogBase.get(256 + i)         // code body
    .for (var f = 0; f < spirogFixups.size(); f++) {
        .word spirogFixups.get(f)
    }
    .word $FFFF

// --- shell - reentrant; boot loads this first ---
.var shellBase     = LoadBinary("../tasks-out/temp/shell.base.bin")
.var shellShift    = LoadBinary("../tasks-out/temp/shell.shift.bin")
.var shellCheck    = LoadBinary("../tasks-out/temp/shell.check.bin")
.var shellNameLen  = shellBase.get(12)
.var shellCodeSize = shellBase.getSize() - 256
.var shellFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < shellBase.getSize(); i++) {
    .if (((shellShift.get(i) & $FF) - (shellBase.get(i) & $FF)) == 1) {
        .eval shellFixups.add(i - 256)
    }
}
.print "shell: " + shellCodeSize + " bytes of code, " + shellFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < shellFixups.size(); f++) {
    .var idx = 256 + shellFixups.get(f)
    .if ((((shellBase.get(idx) & $FF) + 2) & $FF) != (shellCheck.get(idx) & $FF)) {
        .error "shell: fixup verify failed at code offset " + shellFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var shellFi = 0
.for (var i = 256; i < shellBase.getSize(); i++) {
    .if ((shellFi < shellFixups.size()) && (i - 256 == shellFixups.get(shellFi))) {
        .eval shellFi++
    } else {
        .if ((shellBase.get(i) & $FF) != (shellCheck.get(i) & $FF)) {
            .error "shell: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "shell: relocation verification OK"

.segmentdef shellFinal [outBin="../tasks-out/shell.bin"]
.segment shellFinal

// magic byte 1 (+0) is shared.
.fill 2, shellBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + shellNameLen, shellBase.get(2 + i)      // flags..name
    .word shellFixups.size()                          // fixup count (16-bit)
    .word 256 + shellCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + shellNameLen > 256) {
        .error "shell: header overflow"
    }
    .fill 256 - (17 + shellNameLen), 0                // pad to the 256-byte page
    .fill shellCodeSize, shellBase.get(256 + i)         // code body
    .for (var f = 0; f < shellFixups.size(); f++) {
        .word shellFixups.get(f)
    }
    .word $FFFF

// --- ps - declares string.lib ---
.var psBase     = LoadBinary("../tasks-out/temp/ps.base.bin")
.var psShift    = LoadBinary("../tasks-out/temp/ps.shift.bin")
.var psCheck    = LoadBinary("../tasks-out/temp/ps.check.bin")
.var psNameLen  = psBase.get(12)
.var psCodeSize = psBase.getSize() - 256
.var psFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < psBase.getSize(); i++) {
    .if (((psShift.get(i) & $FF) - (psBase.get(i) & $FF)) == 1) {
        .eval psFixups.add(i - 256)
    }
}
.print "ps: " + psCodeSize + " bytes of code, " + psFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < psFixups.size(); f++) {
    .var idx = 256 + psFixups.get(f)
    .if ((((psBase.get(idx) & $FF) + 2) & $FF) != (psCheck.get(idx) & $FF)) {
        .error "ps: fixup verify failed at code offset " + psFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var psFi = 0
.for (var i = 256; i < psBase.getSize(); i++) {
    .if ((psFi < psFixups.size()) && (i - 256 == psFixups.get(psFi))) {
        .eval psFi++
    } else {
        .if ((psBase.get(i) & $FF) != (psCheck.get(i) & $FF)) {
            .error "ps: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "ps: relocation verification OK"

.segmentdef psFinal [outBin="../tasks-out/ps.bin"]
.segment psFinal

// magic byte 1 (+0) is shared.
.fill 2, psBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + psNameLen, psBase.get(2 + i)      // flags..name
    .word psFixups.size()                          // fixup count (16-bit)
    .word 256 + psCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + psNameLen > 256) {
        .error "ps: header overflow"
    }
    .fill 256 - (17 + psNameLen), 0                // pad to the 256-byte page
    .fill psCodeSize, psBase.get(256 + i)         // code body
    .for (var f = 0; f < psFixups.size(); f++) {
        .word psFixups.get(f)
    }
    .word $FFFF

// --- dir ---
.var dirBase     = LoadBinary("../tasks-out/temp/dir.base.bin")
.var dirShift    = LoadBinary("../tasks-out/temp/dir.shift.bin")
.var dirCheck    = LoadBinary("../tasks-out/temp/dir.check.bin")
.var dirNameLen  = dirBase.get(12)
.var dirCodeSize = dirBase.getSize() - 256
.var dirFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < dirBase.getSize(); i++) {
    .if (((dirShift.get(i) & $FF) - (dirBase.get(i) & $FF)) == 1) {
        .eval dirFixups.add(i - 256)
    }
}
.print "dir: " + dirCodeSize + " bytes of code, " + dirFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < dirFixups.size(); f++) {
    .var idx = 256 + dirFixups.get(f)
    .if ((((dirBase.get(idx) & $FF) + 2) & $FF) != (dirCheck.get(idx) & $FF)) {
        .error "dir: fixup verify failed at code offset " + dirFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var dirFi = 0
.for (var i = 256; i < dirBase.getSize(); i++) {
    .if ((dirFi < dirFixups.size()) && (i - 256 == dirFixups.get(dirFi))) {
        .eval dirFi++
    } else {
        .if ((dirBase.get(i) & $FF) != (dirCheck.get(i) & $FF)) {
            .error "dir: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "dir: relocation verification OK"

.segmentdef dirFinal [outBin="../tasks-out/dir.bin"]
.segment dirFinal

// magic byte 1 (+0) is shared.
.fill 2, dirBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + dirNameLen, dirBase.get(2 + i)      // flags..name
    .word dirFixups.size()                          // fixup count (16-bit)
    .word 256 + dirCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + dirNameLen > 256) {
        .error "dir: header overflow"
    }
    .fill 256 - (17 + dirNameLen), 0                // pad to the 256-byte page
    .fill dirCodeSize, dirBase.get(256 + i)         // code body
    .for (var f = 0; f < dirFixups.size(); f++) {
        .word dirFixups.get(f)
    }
    .word $FFFF

// --- time ---
.var timeBase     = LoadBinary("../tasks-out/temp/time.base.bin")
.var timeShift    = LoadBinary("../tasks-out/temp/time.shift.bin")
.var timeCheck    = LoadBinary("../tasks-out/temp/time.check.bin")
.var timeNameLen  = timeBase.get(12)
.var timeCodeSize = timeBase.getSize() - 256
.var timeFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < timeBase.getSize(); i++) {
    .if (((timeShift.get(i) & $FF) - (timeBase.get(i) & $FF)) == 1) {
        .eval timeFixups.add(i - 256)
    }
}
.print "time: " + timeCodeSize + " bytes of code, " + timeFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < timeFixups.size(); f++) {
    .var idx = 256 + timeFixups.get(f)
    .if ((((timeBase.get(idx) & $FF) + 2) & $FF) != (timeCheck.get(idx) & $FF)) {
        .error "time: fixup verify failed at code offset " + timeFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var timeFi = 0
.for (var i = 256; i < timeBase.getSize(); i++) {
    .if ((timeFi < timeFixups.size()) && (i - 256 == timeFixups.get(timeFi))) {
        .eval timeFi++
    } else {
        .if ((timeBase.get(i) & $FF) != (timeCheck.get(i) & $FF)) {
            .error "time: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "time: relocation verification OK"

.segmentdef timeFinal [outBin="../tasks-out/time.bin"]
.segment timeFinal

// magic byte 1 (+0) is shared.
.fill 2, timeBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + timeNameLen, timeBase.get(2 + i)      // flags..name
    .word timeFixups.size()                          // fixup count (16-bit)
    .word 256 + timeCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + timeNameLen > 256) {
        .error "time: header overflow"
    }
    .fill 256 - (17 + timeNameLen), 0                // pad to the 256-byte page
    .fill timeCodeSize, timeBase.get(256 + i)         // code body
    .for (var f = 0; f < timeFixups.size(); f++) {
        .word timeFixups.get(f)
    }
    .word $FFFF

// --- kill ---
.var killBase     = LoadBinary("../tasks-out/temp/kill.base.bin")
.var killShift    = LoadBinary("../tasks-out/temp/kill.shift.bin")
.var killCheck    = LoadBinary("../tasks-out/temp/kill.check.bin")
.var killNameLen  = killBase.get(12)
.var killCodeSize = killBase.getSize() - 256
.var killFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < killBase.getSize(); i++) {
    .if (((killShift.get(i) & $FF) - (killBase.get(i) & $FF)) == 1) {
        .eval killFixups.add(i - 256)
    }
}
.print "kill: " + killCodeSize + " bytes of code, " + killFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < killFixups.size(); f++) {
    .var idx = 256 + killFixups.get(f)
    .if ((((killBase.get(idx) & $FF) + 2) & $FF) != (killCheck.get(idx) & $FF)) {
        .error "kill: fixup verify failed at code offset " + killFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var killFi = 0
.for (var i = 256; i < killBase.getSize(); i++) {
    .if ((killFi < killFixups.size()) && (i - 256 == killFixups.get(killFi))) {
        .eval killFi++
    } else {
        .if ((killBase.get(i) & $FF) != (killCheck.get(i) & $FF)) {
            .error "kill: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "kill: relocation verification OK"

.segmentdef killFinal [outBin="../tasks-out/kill.bin"]
.segment killFinal

// magic byte 1 (+0) is shared.
.fill 2, killBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + killNameLen, killBase.get(2 + i)      // flags..name
    .word killFixups.size()                          // fixup count (16-bit)
    .word 256 + killCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + killNameLen > 256) {
        .error "kill: header overflow"
    }
    .fill 256 - (17 + killNameLen), 0                // pad to the 256-byte page
    .fill killCodeSize, killBase.get(256 + i)         // code body
    .for (var f = 0; f < killFixups.size(); f++) {
        .word killFixups.get(f)
    }
    .word $FFFF

// --- pause ---
.var pauseBase     = LoadBinary("../tasks-out/temp/pause.base.bin")
.var pauseShift    = LoadBinary("../tasks-out/temp/pause.shift.bin")
.var pauseCheck    = LoadBinary("../tasks-out/temp/pause.check.bin")
.var pauseNameLen  = pauseBase.get(12)
.var pauseCodeSize = pauseBase.getSize() - 256
.var pauseFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < pauseBase.getSize(); i++) {
    .if (((pauseShift.get(i) & $FF) - (pauseBase.get(i) & $FF)) == 1) {
        .eval pauseFixups.add(i - 256)
    }
}
.print "pause: " + pauseCodeSize + " bytes of code, " + pauseFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < pauseFixups.size(); f++) {
    .var idx = 256 + pauseFixups.get(f)
    .if ((((pauseBase.get(idx) & $FF) + 2) & $FF) != (pauseCheck.get(idx) & $FF)) {
        .error "pause: fixup verify failed at code offset " + pauseFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var pauseFi = 0
.for (var i = 256; i < pauseBase.getSize(); i++) {
    .if ((pauseFi < pauseFixups.size()) && (i - 256 == pauseFixups.get(pauseFi))) {
        .eval pauseFi++
    } else {
        .if ((pauseBase.get(i) & $FF) != (pauseCheck.get(i) & $FF)) {
            .error "pause: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "pause: relocation verification OK"

.segmentdef pauseFinal [outBin="../tasks-out/pause.bin"]
.segment pauseFinal

// magic byte 1 (+0) is shared.
.fill 2, pauseBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + pauseNameLen, pauseBase.get(2 + i)      // flags..name
    .word pauseFixups.size()                          // fixup count (16-bit)
    .word 256 + pauseCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + pauseNameLen > 256) {
        .error "pause: header overflow"
    }
    .fill 256 - (17 + pauseNameLen), 0                // pad to the 256-byte page
    .fill pauseCodeSize, pauseBase.get(256 + i)         // code body
    .for (var f = 0; f < pauseFixups.size(); f++) {
        .word pauseFixups.get(f)
    }
    .word $FFFF

// --- resume ---
.var resumeBase     = LoadBinary("../tasks-out/temp/resume.base.bin")
.var resumeShift    = LoadBinary("../tasks-out/temp/resume.shift.bin")
.var resumeCheck    = LoadBinary("../tasks-out/temp/resume.check.bin")
.var resumeNameLen  = resumeBase.get(12)
.var resumeCodeSize = resumeBase.getSize() - 256
.var resumeFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < resumeBase.getSize(); i++) {
    .if (((resumeShift.get(i) & $FF) - (resumeBase.get(i) & $FF)) == 1) {
        .eval resumeFixups.add(i - 256)
    }
}
.print "resume: " + resumeCodeSize + " bytes of code, " + resumeFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < resumeFixups.size(); f++) {
    .var idx = 256 + resumeFixups.get(f)
    .if ((((resumeBase.get(idx) & $FF) + 2) & $FF) != (resumeCheck.get(idx) & $FF)) {
        .error "resume: fixup verify failed at code offset " + resumeFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var resumeFi = 0
.for (var i = 256; i < resumeBase.getSize(); i++) {
    .if ((resumeFi < resumeFixups.size()) && (i - 256 == resumeFixups.get(resumeFi))) {
        .eval resumeFi++
    } else {
        .if ((resumeBase.get(i) & $FF) != (resumeCheck.get(i) & $FF)) {
            .error "resume: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "resume: relocation verification OK"

.segmentdef resumeFinal [outBin="../tasks-out/resume.bin"]
.segment resumeFinal

// magic byte 1 (+0) is shared.
.fill 2, resumeBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + resumeNameLen, resumeBase.get(2 + i)      // flags..name
    .word resumeFixups.size()                          // fixup count (16-bit)
    .word 256 + resumeCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + resumeNameLen > 256) {
        .error "resume: header overflow"
    }
    .fill 256 - (17 + resumeNameLen), 0                // pad to the 256-byte page
    .fill resumeCodeSize, resumeBase.get(256 + i)         // code body
    .for (var f = 0; f < resumeFixups.size(); f++) {
        .word resumeFixups.get(f)
    }
    .word $FFFF

// --- prio ---
.var prioBase     = LoadBinary("../tasks-out/temp/prio.base.bin")
.var prioShift    = LoadBinary("../tasks-out/temp/prio.shift.bin")
.var prioCheck    = LoadBinary("../tasks-out/temp/prio.check.bin")
.var prioNameLen  = prioBase.get(12)
.var prioCodeSize = prioBase.getSize() - 256
.var prioFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < prioBase.getSize(); i++) {
    .if (((prioShift.get(i) & $FF) - (prioBase.get(i) & $FF)) == 1) {
        .eval prioFixups.add(i - 256)
    }
}
.print "prio: " + prioCodeSize + " bytes of code, " + prioFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < prioFixups.size(); f++) {
    .var idx = 256 + prioFixups.get(f)
    .if ((((prioBase.get(idx) & $FF) + 2) & $FF) != (prioCheck.get(idx) & $FF)) {
        .error "prio: fixup verify failed at code offset " + prioFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var prioFi = 0
.for (var i = 256; i < prioBase.getSize(); i++) {
    .if ((prioFi < prioFixups.size()) && (i - 256 == prioFixups.get(prioFi))) {
        .eval prioFi++
    } else {
        .if ((prioBase.get(i) & $FF) != (prioCheck.get(i) & $FF)) {
            .error "prio: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "prio: relocation verification OK"

.segmentdef prioFinal [outBin="../tasks-out/prio.bin"]
.segment prioFinal

// magic byte 1 (+0) is shared.
.fill 2, prioBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + prioNameLen, prioBase.get(2 + i)      // flags..name
    .word prioFixups.size()                          // fixup count (16-bit)
    .word 256 + prioCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + prioNameLen > 256) {
        .error "prio: header overflow"
    }
    .fill 256 - (17 + prioNameLen), 0                // pad to the 256-byte page
    .fill prioCodeSize, prioBase.get(256 + i)         // code body
    .for (var f = 0; f < prioFixups.size(); f++) {
        .word prioFixups.get(f)
    }
    .word $FFFF

// --- format ---
.var formatBase     = LoadBinary("../tasks-out/temp/format.base.bin")
.var formatShift    = LoadBinary("../tasks-out/temp/format.shift.bin")
.var formatCheck    = LoadBinary("../tasks-out/temp/format.check.bin")
.var formatNameLen  = formatBase.get(12)
.var formatCodeSize = formatBase.getSize() - 256
.var formatFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < formatBase.getSize(); i++) {
    .if (((formatShift.get(i) & $FF) - (formatBase.get(i) & $FF)) == 1) {
        .eval formatFixups.add(i - 256)
    }
}
.print "format: " + formatCodeSize + " bytes of code, " + formatFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < formatFixups.size(); f++) {
    .var idx = 256 + formatFixups.get(f)
    .if ((((formatBase.get(idx) & $FF) + 2) & $FF) != (formatCheck.get(idx) & $FF)) {
        .error "format: fixup verify failed at code offset " + formatFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var formatFi = 0
.for (var i = 256; i < formatBase.getSize(); i++) {
    .if ((formatFi < formatFixups.size()) && (i - 256 == formatFixups.get(formatFi))) {
        .eval formatFi++
    } else {
        .if ((formatBase.get(i) & $FF) != (formatCheck.get(i) & $FF)) {
            .error "format: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "format: relocation verification OK"

.segmentdef formatFinal [outBin="../tasks-out/format.bin"]
.segment formatFinal

// magic byte 1 (+0) is shared.
.fill 2, formatBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + formatNameLen, formatBase.get(2 + i)      // flags..name
    .word formatFixups.size()                          // fixup count (16-bit)
    .word 256 + formatCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + formatNameLen > 256) {
        .error "format: header overflow"
    }
    .fill 256 - (17 + formatNameLen), 0                // pad to the 256-byte page
    .fill formatCodeSize, formatBase.get(256 + i)         // code body
    .for (var f = 0; f < formatFixups.size(); f++) {
        .word formatFixups.get(f)
    }
    .word $FFFF

// --- type ---
.var typeBase     = LoadBinary("../tasks-out/temp/type.base.bin")
.var typeShift    = LoadBinary("../tasks-out/temp/type.shift.bin")
.var typeCheck    = LoadBinary("../tasks-out/temp/type.check.bin")
.var typeNameLen  = typeBase.get(12)
.var typeCodeSize = typeBase.getSize() - 256
.var typeFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < typeBase.getSize(); i++) {
    .if (((typeShift.get(i) & $FF) - (typeBase.get(i) & $FF)) == 1) {
        .eval typeFixups.add(i - 256)
    }
}
.print "type: " + typeCodeSize + " bytes of code, " + typeFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < typeFixups.size(); f++) {
    .var idx = 256 + typeFixups.get(f)
    .if ((((typeBase.get(idx) & $FF) + 2) & $FF) != (typeCheck.get(idx) & $FF)) {
        .error "type: fixup verify failed at code offset " + typeFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var typeFi = 0
.for (var i = 256; i < typeBase.getSize(); i++) {
    .if ((typeFi < typeFixups.size()) && (i - 256 == typeFixups.get(typeFi))) {
        .eval typeFi++
    } else {
        .if ((typeBase.get(i) & $FF) != (typeCheck.get(i) & $FF)) {
            .error "type: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "type: relocation verification OK"

.segmentdef typeFinal [outBin="../tasks-out/type.bin"]
.segment typeFinal

// magic byte 1 (+0) is shared.
.fill 2, typeBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + typeNameLen, typeBase.get(2 + i)      // flags..name
    .word typeFixups.size()                          // fixup count (16-bit)
    .word 256 + typeCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + typeNameLen > 256) {
        .error "type: header overflow"
    }
    .fill 256 - (17 + typeNameLen), 0                // pad to the 256-byte page
    .fill typeCodeSize, typeBase.get(256 + i)         // code body
    .for (var f = 0; f < typeFixups.size(); f++) {
        .word typeFixups.get(f)
    }
    .word $FFFF

// --- setfont ---
.var setfontBase     = LoadBinary("../tasks-out/temp/setfont.base.bin")
.var setfontShift    = LoadBinary("../tasks-out/temp/setfont.shift.bin")
.var setfontCheck    = LoadBinary("../tasks-out/temp/setfont.check.bin")
.var setfontNameLen  = setfontBase.get(12)
.var setfontCodeSize = setfontBase.getSize() - 256
.var setfontFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < setfontBase.getSize(); i++) {
    .if (((setfontShift.get(i) & $FF) - (setfontBase.get(i) & $FF)) == 1) {
        .eval setfontFixups.add(i - 256)
    }
}
.print "setfont: " + setfontCodeSize + " bytes of code, " + setfontFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < setfontFixups.size(); f++) {
    .var idx = 256 + setfontFixups.get(f)
    .if ((((setfontBase.get(idx) & $FF) + 2) & $FF) != (setfontCheck.get(idx) & $FF)) {
        .error "setfont: fixup verify failed at code offset " + setfontFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var setfontFi = 0
.for (var i = 256; i < setfontBase.getSize(); i++) {
    .if ((setfontFi < setfontFixups.size()) && (i - 256 == setfontFixups.get(setfontFi))) {
        .eval setfontFi++
    } else {
        .if ((setfontBase.get(i) & $FF) != (setfontCheck.get(i) & $FF)) {
            .error "setfont: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "setfont: relocation verification OK"

.segmentdef setfontFinal [outBin="../tasks-out/setfont.bin"]
.segment setfontFinal

// magic byte 1 (+0) is shared.
.fill 2, setfontBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + setfontNameLen, setfontBase.get(2 + i)      // flags..name
    .word setfontFixups.size()                          // fixup count (16-bit)
    .word 256 + setfontCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + setfontNameLen > 256) {
        .error "setfont: header overflow"
    }
    .fill 256 - (17 + setfontNameLen), 0                // pad to the 256-byte page
    .fill setfontCodeSize, setfontBase.get(256 + i)         // code body
    .for (var f = 0; f < setfontFixups.size(); f++) {
        .word setfontFixups.get(f)
    }
    .word $FFFF

// --- rename ---
.var renameBase     = LoadBinary("../tasks-out/temp/rename.base.bin")
.var renameShift    = LoadBinary("../tasks-out/temp/rename.shift.bin")
.var renameCheck    = LoadBinary("../tasks-out/temp/rename.check.bin")
.var renameNameLen  = renameBase.get(12)
.var renameCodeSize = renameBase.getSize() - 256
.var renameFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < renameBase.getSize(); i++) {
    .if (((renameShift.get(i) & $FF) - (renameBase.get(i) & $FF)) == 1) {
        .eval renameFixups.add(i - 256)
    }
}
.print "rename: " + renameCodeSize + " bytes of code, " + renameFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < renameFixups.size(); f++) {
    .var idx = 256 + renameFixups.get(f)
    .if ((((renameBase.get(idx) & $FF) + 2) & $FF) != (renameCheck.get(idx) & $FF)) {
        .error "rename: fixup verify failed at code offset " + renameFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var renameFi = 0
.for (var i = 256; i < renameBase.getSize(); i++) {
    .if ((renameFi < renameFixups.size()) && (i - 256 == renameFixups.get(renameFi))) {
        .eval renameFi++
    } else {
        .if ((renameBase.get(i) & $FF) != (renameCheck.get(i) & $FF)) {
            .error "rename: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "rename: relocation verification OK"

.segmentdef renameFinal [outBin="../tasks-out/rename.bin"]
.segment renameFinal

// magic byte 1 (+0) is shared.
.fill 2, renameBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + renameNameLen, renameBase.get(2 + i)      // flags..name
    .word renameFixups.size()                          // fixup count (16-bit)
    .word 256 + renameCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + renameNameLen > 256) {
        .error "rename: header overflow"
    }
    .fill 256 - (17 + renameNameLen), 0                // pad to the 256-byte page
    .fill renameCodeSize, renameBase.get(256 + i)         // code body
    .for (var f = 0; f < renameFixups.size(); f++) {
        .word renameFixups.get(f)
    }
    .word $FFFF

// --- copy - runtime kMalloc buffer, freed on exit ---
.var copyBase     = LoadBinary("../tasks-out/temp/copy.base.bin")
.var copyShift    = LoadBinary("../tasks-out/temp/copy.shift.bin")
.var copyCheck    = LoadBinary("../tasks-out/temp/copy.check.bin")
.var copyNameLen  = copyBase.get(12)
.var copyCodeSize = copyBase.getSize() - 256
.var copyFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < copyBase.getSize(); i++) {
    .if (((copyShift.get(i) & $FF) - (copyBase.get(i) & $FF)) == 1) {
        .eval copyFixups.add(i - 256)
    }
}
.print "copy: " + copyCodeSize + " bytes of code, " + copyFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < copyFixups.size(); f++) {
    .var idx = 256 + copyFixups.get(f)
    .if ((((copyBase.get(idx) & $FF) + 2) & $FF) != (copyCheck.get(idx) & $FF)) {
        .error "copy: fixup verify failed at code offset " + copyFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var copyFi = 0
.for (var i = 256; i < copyBase.getSize(); i++) {
    .if ((copyFi < copyFixups.size()) && (i - 256 == copyFixups.get(copyFi))) {
        .eval copyFi++
    } else {
        .if ((copyBase.get(i) & $FF) != (copyCheck.get(i) & $FF)) {
            .error "copy: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "copy: relocation verification OK"

.segmentdef copyFinal [outBin="../tasks-out/copy.bin"]
.segment copyFinal

// magic byte 1 (+0) is shared.
.fill 2, copyBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + copyNameLen, copyBase.get(2 + i)      // flags..name
    .word copyFixups.size()                          // fixup count (16-bit)
    .word 256 + copyCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + copyNameLen > 256) {
        .error "copy: header overflow"
    }
    .fill 256 - (17 + copyNameLen), 0                // pad to the 256-byte page
    .fill copyCodeSize, copyBase.get(256 + i)         // code body
    .for (var f = 0; f < copyFixups.size(); f++) {
        .word copyFixups.get(f)
    }
    .word $FFFF

// --- del ---
.var delBase     = LoadBinary("../tasks-out/temp/del.base.bin")
.var delShift    = LoadBinary("../tasks-out/temp/del.shift.bin")
.var delCheck    = LoadBinary("../tasks-out/temp/del.check.bin")
.var delNameLen  = delBase.get(12)
.var delCodeSize = delBase.getSize() - 256
.var delFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < delBase.getSize(); i++) {
    .if (((delShift.get(i) & $FF) - (delBase.get(i) & $FF)) == 1) {
        .eval delFixups.add(i - 256)
    }
}
.print "del: " + delCodeSize + " bytes of code, " + delFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < delFixups.size(); f++) {
    .var idx = 256 + delFixups.get(f)
    .if ((((delBase.get(idx) & $FF) + 2) & $FF) != (delCheck.get(idx) & $FF)) {
        .error "del: fixup verify failed at code offset " + delFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var delFi = 0
.for (var i = 256; i < delBase.getSize(); i++) {
    .if ((delFi < delFixups.size()) && (i - 256 == delFixups.get(delFi))) {
        .eval delFi++
    } else {
        .if ((delBase.get(i) & $FF) != (delCheck.get(i) & $FF)) {
            .error "del: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "del: relocation verification OK"

.segmentdef delFinal [outBin="../tasks-out/del.bin"]
.segment delFinal

// magic byte 1 (+0) is shared.
.fill 2, delBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + delNameLen, delBase.get(2 + i)      // flags..name
    .word delFixups.size()                          // fixup count (16-bit)
    .word 256 + delCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + delNameLen > 256) {
        .error "del: header overflow"
    }
    .fill 256 - (17 + delNameLen), 0                // pad to the 256-byte page
    .fill delCodeSize, delBase.get(256 + i)         // code body
    .for (var f = 0; f < delFixups.size(); f++) {
        .word delFixups.get(f)
    }
    .word $FFFF

// --- compact ---
.var compactBase     = LoadBinary("../tasks-out/temp/compact.base.bin")
.var compactShift    = LoadBinary("../tasks-out/temp/compact.shift.bin")
.var compactCheck    = LoadBinary("../tasks-out/temp/compact.check.bin")
.var compactNameLen  = compactBase.get(12)
.var compactCodeSize = compactBase.getSize() - 256
.var compactFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < compactBase.getSize(); i++) {
    .if (((compactShift.get(i) & $FF) - (compactBase.get(i) & $FF)) == 1) {
        .eval compactFixups.add(i - 256)
    }
}
.print "compact: " + compactCodeSize + " bytes of code, " + compactFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < compactFixups.size(); f++) {
    .var idx = 256 + compactFixups.get(f)
    .if ((((compactBase.get(idx) & $FF) + 2) & $FF) != (compactCheck.get(idx) & $FF)) {
        .error "compact: fixup verify failed at code offset " + compactFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var compactFi = 0
.for (var i = 256; i < compactBase.getSize(); i++) {
    .if ((compactFi < compactFixups.size()) && (i - 256 == compactFixups.get(compactFi))) {
        .eval compactFi++
    } else {
        .if ((compactBase.get(i) & $FF) != (compactCheck.get(i) & $FF)) {
            .error "compact: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "compact: relocation verification OK"

.segmentdef compactFinal [outBin="../tasks-out/compact.bin"]
.segment compactFinal

// magic byte 1 (+0) is shared.
.fill 2, compactBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + compactNameLen, compactBase.get(2 + i)      // flags..name
    .word compactFixups.size()                          // fixup count (16-bit)
    .word 256 + compactCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + compactNameLen > 256) {
        .error "compact: header overflow"
    }
    .fill 256 - (17 + compactNameLen), 0                // pad to the 256-byte page
    .fill compactCodeSize, compactBase.get(256 + i)         // code body
    .for (var f = 0; f < compactFixups.size(); f++) {
        .word compactFixups.get(f)
    }
    .word $FFFF

// --- banner - 2-page runtime kMalloc row buffer, freed on exit ---
.var bannerBase     = LoadBinary("../tasks-out/temp/banner.base.bin")
.var bannerShift    = LoadBinary("../tasks-out/temp/banner.shift.bin")
.var bannerCheck    = LoadBinary("../tasks-out/temp/banner.check.bin")
.var bannerNameLen  = bannerBase.get(12)
.var bannerCodeSize = bannerBase.getSize() - 256
.var bannerFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < bannerBase.getSize(); i++) {
    .if (((bannerShift.get(i) & $FF) - (bannerBase.get(i) & $FF)) == 1) {
        .eval bannerFixups.add(i - 256)
    }
}
.print "banner: " + bannerCodeSize + " bytes of code, " + bannerFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < bannerFixups.size(); f++) {
    .var idx = 256 + bannerFixups.get(f)
    .if ((((bannerBase.get(idx) & $FF) + 2) & $FF) != (bannerCheck.get(idx) & $FF)) {
        .error "banner: fixup verify failed at code offset " + bannerFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var bannerFi = 0
.for (var i = 256; i < bannerBase.getSize(); i++) {
    .if ((bannerFi < bannerFixups.size()) && (i - 256 == bannerFixups.get(bannerFi))) {
        .eval bannerFi++
    } else {
        .if ((bannerBase.get(i) & $FF) != (bannerCheck.get(i) & $FF)) {
            .error "banner: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "banner: relocation verification OK"

.segmentdef bannerFinal [outBin="../tasks-out/banner.bin"]
.segment bannerFinal

// magic byte 1 (+0) is shared.
.fill 2, bannerBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + bannerNameLen, bannerBase.get(2 + i)      // flags..name
    .word bannerFixups.size()                          // fixup count (16-bit)
    .word 256 + bannerCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + bannerNameLen > 256) {
        .error "banner: header overflow"
    }
    .fill 256 - (17 + bannerNameLen), 0                // pad to the 256-byte page
    .fill bannerCodeSize, bannerBase.get(256 + i)         // code body
    .for (var f = 0; f < bannerFixups.size(); f++) {
        .word bannerFixups.get(f)
    }
    .word $FFFF

// --- edit - KF_NOT_EVICTABLE + KF_RE_RUNNABLE + KF_TAKES_ARGS ($69); document chunks and the clipboard are runtime kMalloc blocks ---
.var editBase     = LoadBinary("../tasks-out/temp/edit.base.bin")
.var editShift    = LoadBinary("../tasks-out/temp/edit.shift.bin")
.var editCheck    = LoadBinary("../tasks-out/temp/edit.check.bin")
.var editNameLen  = editBase.get(12)
.var editCodeSize = editBase.getSize() - 256
.var editFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < editBase.getSize(); i++) {
    .if (((editShift.get(i) & $FF) - (editBase.get(i) & $FF)) == 1) {
        .eval editFixups.add(i - 256)
    }
}
.print "edit: " + editCodeSize + " bytes of code, " + editFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < editFixups.size(); f++) {
    .var idx = 256 + editFixups.get(f)
    .if ((((editBase.get(idx) & $FF) + 2) & $FF) != (editCheck.get(idx) & $FF)) {
        .error "edit: fixup verify failed at code offset " + editFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var editFi = 0
.for (var i = 256; i < editBase.getSize(); i++) {
    .if ((editFi < editFixups.size()) && (i - 256 == editFixups.get(editFi))) {
        .eval editFi++
    } else {
        .if ((editBase.get(i) & $FF) != (editCheck.get(i) & $FF)) {
            .error "edit: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "edit: relocation verification OK"

.segmentdef editFinal [outBin="../tasks-out/edit.bin"]
.segment editFinal

// magic byte 1 (+0) is shared.
.fill 2, editBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + editNameLen, editBase.get(2 + i)      // flags..name
    .word editFixups.size()                          // fixup count (16-bit)
    .word 256 + editCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + editNameLen > 256) {
        .error "edit: header overflow"
    }
    .fill 256 - (17 + editNameLen), 0                // pad to the 256-byte page
    .fill editCodeSize, editBase.get(256 + i)         // code body
    .for (var f = 0; f < editFixups.size(); f++) {
        .word editFixups.get(f)
    }
    .word $FFFF

// --- sidplay - thin wrapper over sid.lib: finds/loads the .mus via filesys.lib + kMalloc, calls kSidPlay(kPtr=tune, kArg=delay, kArg+1=repeats), frees, exits; on failure reports on a temporary screen and waits for a key. libMask $0022 (filesys bit 1 + sid bit 5), ZP $02 len 9 ---
.var sidplayBase     = LoadBinary("../tasks-out/temp/sidplay.base.bin")
.var sidplayShift    = LoadBinary("../tasks-out/temp/sidplay.shift.bin")
.var sidplayCheck    = LoadBinary("../tasks-out/temp/sidplay.check.bin")
.var sidplayNameLen  = sidplayBase.get(12)
.var sidplayCodeSize = sidplayBase.getSize() - 256
.var sidplayFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < sidplayBase.getSize(); i++) {
    .if (((sidplayShift.get(i) & $FF) - (sidplayBase.get(i) & $FF)) == 1) {
        .eval sidplayFixups.add(i - 256)
    }
}
.print "sidplay: " + sidplayCodeSize + " bytes of code, " + sidplayFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < sidplayFixups.size(); f++) {
    .var idx = 256 + sidplayFixups.get(f)
    .if ((((sidplayBase.get(idx) & $FF) + 2) & $FF) != (sidplayCheck.get(idx) & $FF)) {
        .error "sidplay: fixup verify failed at code offset " + sidplayFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var sidplayFi = 0
.for (var i = 256; i < sidplayBase.getSize(); i++) {
    .if ((sidplayFi < sidplayFixups.size()) && (i - 256 == sidplayFixups.get(sidplayFi))) {
        .eval sidplayFi++
    } else {
        .if ((sidplayBase.get(i) & $FF) != (sidplayCheck.get(i) & $FF)) {
            .error "sidplay: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "sidplay: relocation verification OK"

.segmentdef sidplayFinal [outBin="../tasks-out/sidplay.bin"]
.segment sidplayFinal

// magic byte 1 (+0) is shared.
.fill 2, sidplayBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + sidplayNameLen, sidplayBase.get(2 + i)      // flags..name
    .word sidplayFixups.size()                          // fixup count (16-bit)
    .word 256 + sidplayCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + sidplayNameLen > 256) {
        .error "sidplay: header overflow"
    }
    .fill 256 - (17 + sidplayNameLen), 0                // pad to the 256-byte page
    .fill sidplayCodeSize, sidplayBase.get(256 + i)         // code body
    .for (var f = 0; f < sidplayFixups.size(); f++) {
        .word sidplayFixups.get(f)
    }
    .word $FFFF

// --- sidrst - zero fixups ---
.var sidrstBase     = LoadBinary("../tasks-out/temp/sidrst.base.bin")
.var sidrstShift    = LoadBinary("../tasks-out/temp/sidrst.shift.bin")
.var sidrstCheck    = LoadBinary("../tasks-out/temp/sidrst.check.bin")
.var sidrstNameLen  = sidrstBase.get(12)
.var sidrstCodeSize = sidrstBase.getSize() - 256
.var sidrstFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < sidrstBase.getSize(); i++) {
    .if (((sidrstShift.get(i) & $FF) - (sidrstBase.get(i) & $FF)) == 1) {
        .eval sidrstFixups.add(i - 256)
    }
}
.print "sidrst: " + sidrstCodeSize + " bytes of code, " + sidrstFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < sidrstFixups.size(); f++) {
    .var idx = 256 + sidrstFixups.get(f)
    .if ((((sidrstBase.get(idx) & $FF) + 2) & $FF) != (sidrstCheck.get(idx) & $FF)) {
        .error "sidrst: fixup verify failed at code offset " + sidrstFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var sidrstFi = 0
.for (var i = 256; i < sidrstBase.getSize(); i++) {
    .if ((sidrstFi < sidrstFixups.size()) && (i - 256 == sidrstFixups.get(sidrstFi))) {
        .eval sidrstFi++
    } else {
        .if ((sidrstBase.get(i) & $FF) != (sidrstCheck.get(i) & $FF)) {
            .error "sidrst: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "sidrst: relocation verification OK"

.segmentdef sidrstFinal [outBin="../tasks-out/sidrst.bin"]
.segment sidrstFinal

// magic byte 1 (+0) is shared.
.fill 2, sidrstBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + sidrstNameLen, sidrstBase.get(2 + i)      // flags..name
    .word sidrstFixups.size()                          // fixup count (16-bit)
    .word 256 + sidrstCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + sidrstNameLen > 256) {
        .error "sidrst: header overflow"
    }
    .fill 256 - (17 + sidrstNameLen), 0                // pad to the 256-byte page
    .fill sidrstCodeSize, sidrstBase.get(256 + i)         // code body
    .for (var f = 0; f < sidrstFixups.size(); f++) {
        .word sidrstFixups.get(f)
    }
    .word $FFFF

// --- blink ---
.var blinkBase     = LoadBinary("../tasks-out/temp/blink.base.bin")
.var blinkShift    = LoadBinary("../tasks-out/temp/blink.shift.bin")
.var blinkCheck    = LoadBinary("../tasks-out/temp/blink.check.bin")
.var blinkNameLen  = blinkBase.get(12)
.var blinkCodeSize = blinkBase.getSize() - 256
.var blinkFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < blinkBase.getSize(); i++) {
    .if (((blinkShift.get(i) & $FF) - (blinkBase.get(i) & $FF)) == 1) {
        .eval blinkFixups.add(i - 256)
    }
}
.print "blink: " + blinkCodeSize + " bytes of code, " + blinkFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < blinkFixups.size(); f++) {
    .var idx = 256 + blinkFixups.get(f)
    .if ((((blinkBase.get(idx) & $FF) + 2) & $FF) != (blinkCheck.get(idx) & $FF)) {
        .error "blink: fixup verify failed at code offset " + blinkFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var blinkFi = 0
.for (var i = 256; i < blinkBase.getSize(); i++) {
    .if ((blinkFi < blinkFixups.size()) && (i - 256 == blinkFixups.get(blinkFi))) {
        .eval blinkFi++
    } else {
        .if ((blinkBase.get(i) & $FF) != (blinkCheck.get(i) & $FF)) {
            .error "blink: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "blink: relocation verification OK"

.segmentdef blinkFinal [outBin="../tasks-out/blink.bin"]
.segment blinkFinal

// magic byte 1 (+0) is shared.
.fill 2, blinkBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + blinkNameLen, blinkBase.get(2 + i)      // flags..name
    .word blinkFixups.size()                          // fixup count (16-bit)
    .word 256 + blinkCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + blinkNameLen > 256) {
        .error "blink: header overflow"
    }
    .fill 256 - (17 + blinkNameLen), 0                // pad to the 256-byte page
    .fill blinkCodeSize, blinkBase.get(256 + i)         // code body
    .for (var f = 0; f < blinkFixups.size(); f++) {
        .word blinkFixups.get(f)
    }
    .word $FFFF

// --- keyrpt ---
.var keyrptBase     = LoadBinary("../tasks-out/temp/keyrpt.base.bin")
.var keyrptShift    = LoadBinary("../tasks-out/temp/keyrpt.shift.bin")
.var keyrptCheck    = LoadBinary("../tasks-out/temp/keyrpt.check.bin")
.var keyrptNameLen  = keyrptBase.get(12)
.var keyrptCodeSize = keyrptBase.getSize() - 256
.var keyrptFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < keyrptBase.getSize(); i++) {
    .if (((keyrptShift.get(i) & $FF) - (keyrptBase.get(i) & $FF)) == 1) {
        .eval keyrptFixups.add(i - 256)
    }
}
.print "keyrpt: " + keyrptCodeSize + " bytes of code, " + keyrptFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < keyrptFixups.size(); f++) {
    .var idx = 256 + keyrptFixups.get(f)
    .if ((((keyrptBase.get(idx) & $FF) + 2) & $FF) != (keyrptCheck.get(idx) & $FF)) {
        .error "keyrpt: fixup verify failed at code offset " + keyrptFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var keyrptFi = 0
.for (var i = 256; i < keyrptBase.getSize(); i++) {
    .if ((keyrptFi < keyrptFixups.size()) && (i - 256 == keyrptFixups.get(keyrptFi))) {
        .eval keyrptFi++
    } else {
        .if ((keyrptBase.get(i) & $FF) != (keyrptCheck.get(i) & $FF)) {
            .error "keyrpt: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "keyrpt: relocation verification OK"

.segmentdef keyrptFinal [outBin="../tasks-out/keyrpt.bin"]
.segment keyrptFinal

// magic byte 1 (+0) is shared.
.fill 2, keyrptBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + keyrptNameLen, keyrptBase.get(2 + i)      // flags..name
    .word keyrptFixups.size()                          // fixup count (16-bit)
    .word 256 + keyrptCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + keyrptNameLen > 256) {
        .error "keyrpt: header overflow"
    }
    .fill 256 - (17 + keyrptNameLen), 0                // pad to the 256-byte page
    .fill keyrptCodeSize, keyrptBase.get(256 + i)         // code body
    .for (var f = 0; f < keyrptFixups.size(); f++) {
        .word keyrptFixups.get(f)
    }
    .word $FFFF

// --- speed - declares the machine's speed; scales blink/keyrpt and feeds sysSpeedShift ---
.var speedBase     = LoadBinary("../tasks-out/temp/speed.base.bin")
.var speedShift    = LoadBinary("../tasks-out/temp/speed.shift.bin")
.var speedCheck    = LoadBinary("../tasks-out/temp/speed.check.bin")
.var speedNameLen  = speedBase.get(12)
.var speedCodeSize = speedBase.getSize() - 256
.var speedFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < speedBase.getSize(); i++) {
    .if (((speedShift.get(i) & $FF) - (speedBase.get(i) & $FF)) == 1) {
        .eval speedFixups.add(i - 256)
    }
}
.print "speed: " + speedCodeSize + " bytes of code, " + speedFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < speedFixups.size(); f++) {
    .var idx = 256 + speedFixups.get(f)
    .if ((((speedBase.get(idx) & $FF) + 2) & $FF) != (speedCheck.get(idx) & $FF)) {
        .error "speed: fixup verify failed at code offset " + speedFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var speedFi = 0
.for (var i = 256; i < speedBase.getSize(); i++) {
    .if ((speedFi < speedFixups.size()) && (i - 256 == speedFixups.get(speedFi))) {
        .eval speedFi++
    } else {
        .if ((speedBase.get(i) & $FF) != (speedCheck.get(i) & $FF)) {
            .error "speed: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "speed: relocation verification OK"

.segmentdef speedFinal [outBin="../tasks-out/speed.bin"]
.segment speedFinal

// magic byte 1 (+0) is shared.
.fill 2, speedBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + speedNameLen, speedBase.get(2 + i)      // flags..name
    .word speedFixups.size()                          // fixup count (16-bit)
    .word 256 + speedCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + speedNameLen > 256) {
        .error "speed: header overflow"
    }
    .fill 256 - (17 + speedNameLen), 0                // pad to the 256-byte page
    .fill speedCodeSize, speedBase.get(256 + i)         // code body
    .for (var f = 0; f < speedFixups.size(); f++) {
        .word speedFixups.get(f)
    }
    .word $FFFF

// --- fstest - declares filesys.lib; runtime self-test for the FS bank boundary - type fstest ---
.var fstestBase     = LoadBinary("../tasks-out/temp/fstest.base.bin")
.var fstestShift    = LoadBinary("../tasks-out/temp/fstest.shift.bin")
.var fstestCheck    = LoadBinary("../tasks-out/temp/fstest.check.bin")
.var fstestNameLen  = fstestBase.get(12)
.var fstestCodeSize = fstestBase.getSize() - 256
.var fstestFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < fstestBase.getSize(); i++) {
    .if (((fstestShift.get(i) & $FF) - (fstestBase.get(i) & $FF)) == 1) {
        .eval fstestFixups.add(i - 256)
    }
}
.print "fstest: " + fstestCodeSize + " bytes of code, " + fstestFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < fstestFixups.size(); f++) {
    .var idx = 256 + fstestFixups.get(f)
    .if ((((fstestBase.get(idx) & $FF) + 2) & $FF) != (fstestCheck.get(idx) & $FF)) {
        .error "fstest: fixup verify failed at code offset " + fstestFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var fstestFi = 0
.for (var i = 256; i < fstestBase.getSize(); i++) {
    .if ((fstestFi < fstestFixups.size()) && (i - 256 == fstestFixups.get(fstestFi))) {
        .eval fstestFi++
    } else {
        .if ((fstestBase.get(i) & $FF) != (fstestCheck.get(i) & $FF)) {
            .error "fstest: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "fstest: relocation verification OK"

.segmentdef fstestFinal [outBin="../tasks-out/fstest.bin"]
.segment fstestFinal

// magic byte 1 (+0) is shared.
.fill 2, fstestBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + fstestNameLen, fstestBase.get(2 + i)      // flags..name
    .word fstestFixups.size()                          // fixup count (16-bit)
    .word 256 + fstestCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + fstestNameLen > 256) {
        .error "fstest: header overflow"
    }
    .fill 256 - (17 + fstestNameLen), 0                // pad to the 256-byte page
    .fill fstestCodeSize, fstestBase.get(256 + i)         // code body
    .for (var f = 0; f < fstestFixups.size(); f++) {
        .word fstestFixups.get(f)
    }
    .word $FFFF

// --- ipctest - declares ipc.lib; the ipc.lib runtime self-test (keep - the counterpart of fstest) - spawns two thread producers (channels 1 and 3) and consumes both interleaved, so the backpressure wait and the channel recovery after SYS_SLEEP are both exercised; expect `ipc PASS` ---
.var ipctestBase     = LoadBinary("../tasks-out/temp/ipctest.base.bin")
.var ipctestShift    = LoadBinary("../tasks-out/temp/ipctest.shift.bin")
.var ipctestCheck    = LoadBinary("../tasks-out/temp/ipctest.check.bin")
.var ipctestNameLen  = ipctestBase.get(12)
.var ipctestCodeSize = ipctestBase.getSize() - 256
.var ipctestFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < ipctestBase.getSize(); i++) {
    .if (((ipctestShift.get(i) & $FF) - (ipctestBase.get(i) & $FF)) == 1) {
        .eval ipctestFixups.add(i - 256)
    }
}
.print "ipctest: " + ipctestCodeSize + " bytes of code, " + ipctestFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < ipctestFixups.size(); f++) {
    .var idx = 256 + ipctestFixups.get(f)
    .if ((((ipctestBase.get(idx) & $FF) + 2) & $FF) != (ipctestCheck.get(idx) & $FF)) {
        .error "ipctest: fixup verify failed at code offset " + ipctestFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var ipctestFi = 0
.for (var i = 256; i < ipctestBase.getSize(); i++) {
    .if ((ipctestFi < ipctestFixups.size()) && (i - 256 == ipctestFixups.get(ipctestFi))) {
        .eval ipctestFi++
    } else {
        .if ((ipctestBase.get(i) & $FF) != (ipctestCheck.get(i) & $FF)) {
            .error "ipctest: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "ipctest: relocation verification OK"

.segmentdef ipctestFinal [outBin="../tasks-out/ipctest.bin"]
.segment ipctestFinal

// magic byte 1 (+0) is shared.
.fill 2, ipctestBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + ipctestNameLen, ipctestBase.get(2 + i)      // flags..name
    .word ipctestFixups.size()                          // fixup count (16-bit)
    .word 256 + ipctestCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + ipctestNameLen > 256) {
        .error "ipctest: header overflow"
    }
    .fill 256 - (17 + ipctestNameLen), 0                // pad to the 256-byte page
    .fill ipctestCodeSize, ipctestBase.get(256 + i)         // code body
    .for (var f = 0; f < ipctestFixups.size(); f++) {
        .word ipctestFixups.get(f)
    }
    .word $FFFF

// --- sprdemo - no libs - eight bouncing balls: all 8 sprite slots, REU sheet art (ball.spr), wall + ball-ball collisions, one update per displayed frame ---
.var sprdemoBase     = LoadBinary("../tasks-out/temp/sprdemo.base.bin")
.var sprdemoShift    = LoadBinary("../tasks-out/temp/sprdemo.shift.bin")
.var sprdemoCheck    = LoadBinary("../tasks-out/temp/sprdemo.check.bin")
.var sprdemoNameLen  = sprdemoBase.get(12)
.var sprdemoCodeSize = sprdemoBase.getSize() - 256
.var sprdemoFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < sprdemoBase.getSize(); i++) {
    .if (((sprdemoShift.get(i) & $FF) - (sprdemoBase.get(i) & $FF)) == 1) {
        .eval sprdemoFixups.add(i - 256)
    }
}
.print "sprdemo: " + sprdemoCodeSize + " bytes of code, " + sprdemoFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < sprdemoFixups.size(); f++) {
    .var idx = 256 + sprdemoFixups.get(f)
    .if ((((sprdemoBase.get(idx) & $FF) + 2) & $FF) != (sprdemoCheck.get(idx) & $FF)) {
        .error "sprdemo: fixup verify failed at code offset " + sprdemoFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var sprdemoFi = 0
.for (var i = 256; i < sprdemoBase.getSize(); i++) {
    .if ((sprdemoFi < sprdemoFixups.size()) && (i - 256 == sprdemoFixups.get(sprdemoFi))) {
        .eval sprdemoFi++
    } else {
        .if ((sprdemoBase.get(i) & $FF) != (sprdemoCheck.get(i) & $FF)) {
            .error "sprdemo: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "sprdemo: relocation verification OK"

.segmentdef sprdemoFinal [outBin="../tasks-out/sprdemo.bin"]
.segment sprdemoFinal

// magic byte 1 (+0) is shared.
.fill 2, sprdemoBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + sprdemoNameLen, sprdemoBase.get(2 + i)      // flags..name
    .word sprdemoFixups.size()                          // fixup count (16-bit)
    .word 256 + sprdemoCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + sprdemoNameLen > 256) {
        .error "sprdemo: header overflow"
    }
    .fill 256 - (17 + sprdemoNameLen), 0                // pad to the 256-byte page
    .fill sprdemoCodeSize, sprdemoBase.get(256 + i)         // code body
    .for (var f = 0; f < sprdemoFixups.size(); f++) {
        .word sprdemoFixups.get(f)
    }
    .word $FFFF

// --- godzi - no libs, KF_TAKES_ARGS, single-instance (owns the SID, so a second run is refused) - port of reference/godzi.asm + reference/main.asm: boat walker crosses x=256 (two overlapping layers, black rigging over a light-blue hull), Godzilla flaps between two poses (four slots, REU art boat.spr/godzi.spr, one update per displayed frame); spawns + restarts sidplay for music (commodo 2 in mode 1, fsonata 2 in mode 2), and on q kills it + spawns sidrst (killing a SID player leaves the chip holding its note) ---
.var godziBase     = LoadBinary("../tasks-out/temp/godzi.base.bin")
.var godziShift    = LoadBinary("../tasks-out/temp/godzi.shift.bin")
.var godziCheck    = LoadBinary("../tasks-out/temp/godzi.check.bin")
.var godziNameLen  = godziBase.get(12)
.var godziCodeSize = godziBase.getSize() - 256
.var godziFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < godziBase.getSize(); i++) {
    .if (((godziShift.get(i) & $FF) - (godziBase.get(i) & $FF)) == 1) {
        .eval godziFixups.add(i - 256)
    }
}
.print "godzi: " + godziCodeSize + " bytes of code, " + godziFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < godziFixups.size(); f++) {
    .var idx = 256 + godziFixups.get(f)
    .if ((((godziBase.get(idx) & $FF) + 2) & $FF) != (godziCheck.get(idx) & $FF)) {
        .error "godzi: fixup verify failed at code offset " + godziFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var godziFi = 0
.for (var i = 256; i < godziBase.getSize(); i++) {
    .if ((godziFi < godziFixups.size()) && (i - 256 == godziFixups.get(godziFi))) {
        .eval godziFi++
    } else {
        .if ((godziBase.get(i) & $FF) != (godziCheck.get(i) & $FF)) {
            .error "godzi: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "godzi: relocation verification OK"

.segmentdef godziFinal [outBin="../tasks-out/godzi.bin"]
.segment godziFinal

// magic byte 1 (+0) is shared.
.fill 2, godziBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + godziNameLen, godziBase.get(2 + i)      // flags..name
    .word godziFixups.size()                          // fixup count (16-bit)
    .word 256 + godziCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + godziNameLen > 256) {
        .error "godzi: header overflow"
    }
    .fill 256 - (17 + godziNameLen), 0                // pad to the 256-byte page
    .fill godziCodeSize, godziBase.get(256 + i)         // code body
    .for (var f = 0; f < godziFixups.size(); f++) {
        .word godziFixups.get(f)
    }
    .word $FFFF

// --- gortris - KF_NOT_EVICTABLE ($41), libMask $0022 (filesys + sid); port of wiebow/tetris.c64 (MIT) - a 10x20 board in the task image, the reference's own 4x4 piece frames and its Select/Animate/Block*/CheckLines/FlashLines/RemoveLines logic, rendered as 8-byte block glyphs; attract loop (4 screens on its 254-frame delay) -> level select (its 0-9 grid, flashing digit) -> play -> game over -> attract (the game over runs the reference's animation: the well fills with blocks a row a frame, is held full ~3 s, empties, and then the text is printed inside the well); hi scores = the reference's 3-entry table (7-char name + 3 BCD score bytes) kept in the REU FS as gortris.hi, shown on the select screen, checked at game over (a tie does not displace), with its own yielding name editor (kReadString spins) that types into the 7-cell field under the kernel's blinking block cursor (kEnableCursor/kBlinkReset; a key from kReadKey is a SCREEN CODE, so the letters are $01-$1A and a 'printable = >= $20' test discards all of them); this is why the task HOLDS filesys.lib for the session instead of handing it back like sidplay); level rule = its 10 lines/level, fall delay -4 per level floored at 4, line score x(level+1); music is sid.lib called DIRECTLY by a thread child it spawns (ZP $02-$0A declared by that child): the tune is kMalloc'd from the REU FS at startup (tetris.mus -- our own three-voice SID arrangement of the Tetris theme Korobeiniki: the melody and bass line were read from a MIDI made from a fan piano sheet (NinSheetMusic sheet 574, 'Tetris - A Theme (Korobeiniki)', piano arrangement by Olimar12345, credited there to Hirokazu Tanaka's game arrangement, Nintendo (C) 1989 -- see NOTICE), the chords fitted from the notes sounding, and the SID voicing, form and mix ours, by tools/gen-mus.py; because a `.sid` is a program, not a score, so no converter exists) -- that file plays ONCE (no HED/TAL loop in it) and the child passes kSidPlay repeats = 0 so the PLAYER repeats it, which is why `run sidplay tetris` stops after one pass; `m` toggles the music from any screen (the play panel shows 'music off', and the mute survives focus and mode changes because gtMusicStart refuses while gtMusOff is set), filesys.lib NOT handed back (kLibHold is one-way and the hi-score table has to be written), hushed on focus loss and the child reaped + buffer freed on exit; attract screens show for 1016 displayed frames (~20 s) each; owns the display + kbFocus; keys: , . move, a s turn, ENTER step down (1 a row), SPACE hard drop to the floor + settle (2 a row, this port's own move), p pause, q quit; also reads the reference's joystick (port 2, UNTESTED: UP rotate-CCW, DOWN soft drop, LEFT/RIGHT move, FIRE rotate-CW/start) and ports its INPUT repeat tuning -- INPUTDELAY 10 frames between repeats, halved for the drop keys, adjustable on the select screen with '-'/'+' ---
.var gortrisBase     = LoadBinary("../tasks-out/temp/gortris.base.bin")
.var gortrisShift    = LoadBinary("../tasks-out/temp/gortris.shift.bin")
.var gortrisCheck    = LoadBinary("../tasks-out/temp/gortris.check.bin")
.var gortrisNameLen  = gortrisBase.get(12)
.var gortrisCodeSize = gortrisBase.getSize() - 256
.var gortrisFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < gortrisBase.getSize(); i++) {
    .if (((gortrisShift.get(i) & $FF) - (gortrisBase.get(i) & $FF)) == 1) {
        .eval gortrisFixups.add(i - 256)
    }
}
.print "gortris: " + gortrisCodeSize + " bytes of code, " + gortrisFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < gortrisFixups.size(); f++) {
    .var idx = 256 + gortrisFixups.get(f)
    .if ((((gortrisBase.get(idx) & $FF) + 2) & $FF) != (gortrisCheck.get(idx) & $FF)) {
        .error "gortris: fixup verify failed at code offset " + gortrisFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var gortrisFi = 0
.for (var i = 256; i < gortrisBase.getSize(); i++) {
    .if ((gortrisFi < gortrisFixups.size()) && (i - 256 == gortrisFixups.get(gortrisFi))) {
        .eval gortrisFi++
    } else {
        .if ((gortrisBase.get(i) & $FF) != (gortrisCheck.get(i) & $FF)) {
            .error "gortris: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "gortris: relocation verification OK"

.segmentdef gortrisFinal [outBin="../tasks-out/gortris.bin"]
.segment gortrisFinal

// magic byte 1 (+0) is shared.
.fill 2, gortrisBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + gortrisNameLen, gortrisBase.get(2 + i)      // flags..name
    .word gortrisFixups.size()                          // fixup count (16-bit)
    .word 256 + gortrisCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + gortrisNameLen > 256) {
        .error "gortris: header overflow"
    }
    .fill 256 - (17 + gortrisNameLen), 0                // pad to the 256-byte page
    .fill gortrisCodeSize, gortrisBase.get(256 + i)         // code body
    .for (var f = 0; f < gortrisFixups.size(); f++) {
        .word gortrisFixups.get(f)
    }
    .word $FFFF

// --- grknoid - KF_NOT_EVICTABLE ($41), libMask $00A2 (sprite.lib + filesys.lib + sid.lib) - gorkanoid, an Arkanoid/Breakout game: a paddle sprite (gkpad.spr, x-expanded 24->48px) under a wall of glyph bricks on the task's own hires-text surface; the wall is a 48-entry byte array in the task image (0 = a gap, 1-3 = the hits to break the brick) and is the AUTHORITY -- the drawn cells are presentation and nothing reads the surface back; the paddle follows the HELD key (kernel data keybHeld, a LEVEL) one step per DISPLAYED frame, with the step ACCELERATING 1 -> 3 px while the same direction is held (16 displayed frames a gear, reset by a release or a reversal), with 'j' 'k' SPACE and the port-2 joystick (DDRA dance under SEI, bits tested individually) feeding one input byte, and the buttons taken from its newly-down bits; the paddle's x is stored as an offset from its left stop so the whole run fits one byte and kSpritePos's ninth bit is the carry out of the same add; the ball is sprite 1 (gkball.spr, an 8x8 disc in the sheet's TOP-LEFT corner so the sprite position IS the box -- 8 because a 6px box can only be a rounded square) flown in 8.8 fixed point -- x three bytes because 24..344 does not fit one, y two, velocities signed with the sign carried above the fraction, integration an adc chain and a bounce a two's-complement negation; the paddle's angle comes from the hit offset scaled by 8, floored at $0060 so the ball can never climb straight up, plus THE ENGLISH: half the paddle's speed added when it is moving into the ball, using the direction it ACTUALLY travelled (a paddle held against a wall contributes nothing); the bricks are 8 to a row, 4 cells each, so a row is exactly 32px and a level table row is exactly FOUR nibble-packed bytes (two bricks a byte, the left one low -- $11 is two one-hit bricks, and the five levels are six source lines each that ARE the wall); a brick's colour carries BOTH its damage and its position -- the table is indexed by hit points*2 + the column parity, so each hit-point group owns a PAIR of alternating hues (red/orange 3 hits, light green/green 2, yellow/cyan 1); the ball's brick test takes the 2x2 cell block at the ball's own cell, first hit wins, and reverses vx when the hit cell's COLUMN differs from the ball's CENTRE cell and vy when its ROW does (a corner does both) -- the reference this is ported from reversed only vy and so could never break a brick with the ball's side; a hit pays 5 when it only cracks a brick and 10 x the level when it breaks one, kept in 16-bit BINARY and printed by repeated subtraction, never in decimal mode (the timer IRQ does arithmetic, and a keyboard scan landing inside a `sed` window computes a wrong key index -- one of the wrong keys is q, which quits); the frame wall and the HUD are drawn once and no rule ever writes the last row; the game is a MODE MACHINE (attract, play, over) and the attract loop is the only place the controls are shown -- four screens on a 1016-frame delay inside a closed box that covers the whole surface, any key starting a game (and the closed box's bottom edge has to go through kBlitChar, because a print into the last cell of the last row wraps and scrolls the whole surface); five lives, a drawn DEATH LINE across the field's floor (the last pixel line of the cell row GK_BALL_DEADY names, one pixel line per cell through gkGlyphDead), and a game over that is a SHELL screen -- the field cleared, the same box up -- which checks the run's score against gortris's own three-row BCD table (grknoid.hi in the REU FS, the name typed into the row under the kernel's block cursor, saved find-delete-save because the FS delete is a tombstone); the kernel's key queue is consumed only by a LATCH that waits for the keys to be released first (a per-frame idle test drains the very press it is waiting for), and the game-over screen drops back to the attract loop on its own after GK_RETURN_DELAY of an idle keyboard and a displayed surface, while the NAME ENTRY never times out at all (it is a data-entry step: RETURN is what commits the typed name, and the save runs only after it, so the screen waits for the player rather than storing something they never finished); the ball's colour PULSES through the palette's greys (white, light, medium, dark, and back) one shade every 8 displayed frames, stepped from the ball's own frame tail so it freezes with the game and costs one kSpriteReg per shade rather than per frame; the SERVE'S SPEED RAMPS WITH THE LEVEL -- gkLaunch takes vx from gkVxTab (level 1 is the GK_VXMIN floor, +$18 a level, so level 5 is exactly twice level 1) and DERIVES vy as -2 * vx, which holds the launch angle at 26.6 degrees from the vertical on every level, capped by GK_VYMAX ($0180 = 1.5 px/frame, the number gkBallPaddle's y-band proof already assumes) with four asserts pinning the shape so a steeper ramp fails the build instead of the play test, and the level is on the HUD as lv at column 34 with its digit at 37 because a ramped speed a player cannot see is indistinguishable from a bounce going well; and the music is THREE CGSC tunes (katmand/immig/firstdt -- the Compute's Gazette Sid Collection, the archive written for the Sidplayer sid.lib ports) played one per mode by a thread child the task spawns and reaps: katmand in the attract loop, immig in a game, and firstdt ONCE at game over, after which the child hands the attract tune back by itself; the task kMallocs ONE 18-page tune buffer at startup and keeps it for the whole session, sized for the largest tune (katmand); the child loads each mode's tune INTO that buffer, so nothing is allocated or freed on a mode change (a per-tune malloc/free cycle fragments the pool, and an 18-page request then cannot be satisfied), and filesys.lib stays held for the whole session because the score table is written and the child reads a tune file on every switch; a SID the child is REFUSED (another task owns the chip) is not a failure -- the tune is KEPT and the play is retried about once a second, so the music returns by itself, while a tune that will not LOAD -- missing, unreadable, or longer than the buffer -- falls back to the attract tune and then goes quiet WITHOUT a retry, because a kMalloc retry can evict another task; the game-over screen's tune is its CLOCK: it waits for the one-shot to finish (the child sets gkMusDone) and leaves then, or at once on a key, and GK_RETURN_DELAY is only the fallback for a screen with no tune to wait for; losing the display freezes the game (no mode step, keybHeld kept in step, q focused-only) but does NOT hush the music, deliberately unlike gortris ---
.var grknoidBase     = LoadBinary("../tasks-out/temp/grknoid.base.bin")
.var grknoidShift    = LoadBinary("../tasks-out/temp/grknoid.shift.bin")
.var grknoidCheck    = LoadBinary("../tasks-out/temp/grknoid.check.bin")
.var grknoidNameLen  = grknoidBase.get(12)
.var grknoidCodeSize = grknoidBase.getSize() - 256
.var grknoidFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < grknoidBase.getSize(); i++) {
    .if (((grknoidShift.get(i) & $FF) - (grknoidBase.get(i) & $FF)) == 1) {
        .eval grknoidFixups.add(i - 256)
    }
}
.print "grknoid: " + grknoidCodeSize + " bytes of code, " + grknoidFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < grknoidFixups.size(); f++) {
    .var idx = 256 + grknoidFixups.get(f)
    .if ((((grknoidBase.get(idx) & $FF) + 2) & $FF) != (grknoidCheck.get(idx) & $FF)) {
        .error "grknoid: fixup verify failed at code offset " + grknoidFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var grknoidFi = 0
.for (var i = 256; i < grknoidBase.getSize(); i++) {
    .if ((grknoidFi < grknoidFixups.size()) && (i - 256 == grknoidFixups.get(grknoidFi))) {
        .eval grknoidFi++
    } else {
        .if ((grknoidBase.get(i) & $FF) != (grknoidCheck.get(i) & $FF)) {
            .error "grknoid: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "grknoid: relocation verification OK"

.segmentdef grknoidFinal [outBin="../tasks-out/grknoid.bin"]
.segment grknoidFinal

// magic byte 1 (+0) is shared.
.fill 2, grknoidBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + grknoidNameLen, grknoidBase.get(2 + i)      // flags..name
    .word grknoidFixups.size()                          // fixup count (16-bit)
    .word 256 + grknoidCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + grknoidNameLen > 256) {
        .error "grknoid: header overflow"
    }
    .fill 256 - (17 + grknoidNameLen), 0                // pad to the 256-byte page
    .fill grknoidCodeSize, grknoidBase.get(256 + i)         // code body
    .for (var f = 0; f < grknoidFixups.size(); f++) {
        .word grknoidFixups.get(f)
    }
    .word $FFFF

// --- grview - KF_TAKES_ARGS - the .pic viewer: `run grview hires` opens hires.pic (the suffix is appended, as sidplay appends .mus, so the base name is <= 7); 'm' reinterprets the same bytes in the other mode and any other key exits; a failure takes a screen of its own and waits for a key, sidplay-style; declares gfx.lib + filesys.lib ---
.var grviewBase     = LoadBinary("../tasks-out/temp/grview.base.bin")
.var grviewShift    = LoadBinary("../tasks-out/temp/grview.shift.bin")
.var grviewCheck    = LoadBinary("../tasks-out/temp/grview.check.bin")
.var grviewNameLen  = grviewBase.get(12)
.var grviewCodeSize = grviewBase.getSize() - 256
.var grviewFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < grviewBase.getSize(); i++) {
    .if (((grviewShift.get(i) & $FF) - (grviewBase.get(i) & $FF)) == 1) {
        .eval grviewFixups.add(i - 256)
    }
}
.print "grview: " + grviewCodeSize + " bytes of code, " + grviewFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < grviewFixups.size(); f++) {
    .var idx = 256 + grviewFixups.get(f)
    .if ((((grviewBase.get(idx) & $FF) + 2) & $FF) != (grviewCheck.get(idx) & $FF)) {
        .error "grview: fixup verify failed at code offset " + grviewFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var grviewFi = 0
.for (var i = 256; i < grviewBase.getSize(); i++) {
    .if ((grviewFi < grviewFixups.size()) && (i - 256 == grviewFixups.get(grviewFi))) {
        .eval grviewFi++
    } else {
        .if ((grviewBase.get(i) & $FF) != (grviewCheck.get(i) & $FF)) {
            .error "grview: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "grview: relocation verification OK"

.segmentdef grviewFinal [outBin="../tasks-out/grview.bin"]
.segment grviewFinal

// magic byte 1 (+0) is shared.
.fill 2, grviewBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + grviewNameLen, grviewBase.get(2 + i)      // flags..name
    .word grviewFixups.size()                          // fixup count (16-bit)
    .word 256 + grviewCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + grviewNameLen > 256) {
        .error "grview: header overflow"
    }
    .fill 256 - (17 + grviewNameLen), 0                // pad to the 256-byte page
    .fill grviewCodeSize, grviewBase.get(256 + i)         // code body
    .for (var f = 0; f < grviewFixups.size(); f++) {
        .word grviewFixups.get(f)
    }
    .word $FFFF

// --- grpaint - KF_TAKES_ARGS, SINGLE-INSTANCE (NOT KF_RE_RUNNABLE: its 71-page live footprint means two copies plus the shell are the whole pool, so a second run would evict the copy that is running) - the 1351 paint program. The mouseKind gate comes first (off -> a screen of its OWN with a dark grey border, '? mouse off - run mouse 1351', held until a key -- the sidplay pattern, never a shared-shell print), then kAllocScreen takes a bitmap canvas in the MODE AND PALETTE the run resolved (the colour arguments for a new picture, the file's own header for a load, both settled BEFORE the alloc so the surface is never re-coloured afterwards) and the alloc hands it back already cleared, kDisableCursor + the SEI-wrapped kBlinkReset/kSwitchView hand-over shows it without baking the shell's blink block. The pointer is TWO SPRITES stamping ONE shape: pointer.spr carries two hires frames of the classic arrow-pointer silhouette, frame 0 the interior taking the ink (and, while the palette is up, the colour of the swatch under the pointer) and frame 1 the ring taking a FIXED colour, each bound once via kSpriteSheet (a sheet record is per sprite, so the one file needs two calls); it is positioned once per DISPLAYED frame off frameCount at sprite (canvas x + 24, canvas y + 50) -- the window's left edge and its first raster line -- moved by mouse.lib's SIGNED deltas (MOUSE_READ -> kArg+MOUSE_DELTA_*, the y one negated because the 1351's y runs opposite to the screen's), clamped to 0..319 by 0..199. The per-frame reading is not trusted on its own: the pot noise makes a sample read one count high, so the frame is followed as offsets from its first sample and moved by the change in their MINIMUM, which is the reading with no noise on it. Painting: the left button (PA4 of $DC00, the fire line, ACTIVE LOW) draws the segment from where the previous frame's stroke ended to where this frame moved the pointer, one kPlot per pixel -- kPlot is the call that commits a cell's matrix high nibble, so in hires the stroke shows the ink. The walk is the task's own Bresenham with the error held below the major span, which draws a line steeper than 45 degrees as the run of pixels it is rather than a dotted diagonal; a press is an EVENT, so a click marks exactly one pixel, and a pointer moved with the button up writes nothing. Both endpoints are clamped, and the walk only steps towards the destination, so no clip is needed anywhere. 'q' ASKS BEFORE IT LOSES WORK: with the canvas as the file holds it, 'q' drains the key queue, clears the sprite enable and SYS_EXITs (no kFreeScreen) at once; with something drawn since the last save, the first 'q' only raises the band if it is down and starts the SAVE tile FLASHING (gpFlash: once a frame, phase from one bit of frameCount -- 32 frames each way, 0.8 Hz -- re-reading the band's matrix from its file and re-marking on a change), and it is the SAVE tile's own press -- which saves and stays, clearing both GP_WARN and the flash -- or 'q' again WHILE THE BAND IS UP (gpQuitAsk, GP_WARN: a dismissed band is a question nobody can see, so the next 'q' raises it again) that ends it. The palette (step 4.6) is the right button HELD: the canvas's BOTTOM TWO cell rows become a full-width UI band, with the sixteen 8x8-cell swatches filling its left 8 cells across BOTH rows -- colours 0-7 along the top, 8-15 along the bottom, in the order of the C64 palette (the swatch IS its colour). The band is two CELL rows -- a bitmap's cells are its memory, so a cell row of the screen is a contiguous 320-byte run and the band's two are one run of 640 -- and WHAT GOES IN IS A FILE: gpui.bin holds the band's own 640 pixel bytes then its 80 matrix bytes, built by tools/gen-ui.py from assets/ui/gpui.txt, so the task holds no picture and composes nothing. The right button keeps the band with two REU DMAs (gpDma: 640 pixel bytes at $3CC0 and 80 matrix bytes at $0798 into bank 0 $D000/$D280, free there so the filesystem loses no capacity), reads the file's two halves straight into those same addresses (kReuOpen at spawn for BOTH modes -- gpui.bin and gpui_mc.bin, a record each, selected by GP_MODE through gpUiRecOfs, so no press ever scans the directory, because a scan inside a press is a frame the pointer is not sampled over and a click can vanish in it -- then kReuReadAt), and the release puts the saved two runs back. Nothing in the band is ever written while the pointer moves over it: a swatch's colour IS its cell's matrix nibble, so a highlight drawn there would draw over the colour it marks -- the feedback is the pointer's INTERIOR, which takes the colour of the swatch under it (and the current ink off the palette). The release takes that colour as the ink, colours the interior with it and puts the band back; released off the palette it takes nothing. A press ON THE BAND but not on anything DRAWN is a press off the UI and puts the band away: in HIRES the four slot-chip columns are empty band, so the hit test offers no entry there rather than an entry whose press takes nothing. The pointer's ring is a FIXED colour, so the pointer stays visible even at ink 0. The tool tiles sit at the palette's right, each a 2x2-cell square: the PENCIL first -- it is what a fresh canvas starts with, so the default is the leftmost tile -- then the line, the box and the filled box. A tile's entry number (16 and up, counted on from the swatches) minus 16 IS its tool number, which is what makes the picker's arithmetic a subtraction and the mark's the same addition back. Each tile's picture carries its own 1px border with the tool's shape inside it, clear of the border, and the tool in use is told apart by COLOUR -- its four cells go white where the image holds them grey. The shape tools work click to click: the first click anchors and marks its pixel, the second draws from it. What the drawing click leaves behind is the tool's own business -- the LINE keeps the end it drew to, so EVERY click after the first draws from where the last line ended and a run of lines is one click each, while the BOX and the FILLED BOX CONSUME their anchor, so a rectangle is two clicks, every time. The line is drawn with the SAME Bresenham the pencil uses (the anchor becomes GP_LAST and gpSeg commits it, so one walk serves both tools). The box and the filled box hand kBox/kFillBox the two corners sorted into kArg (X1, Y1, width, height; the x pair a signed 16-bit difference, the y pair an unsigned compare of the two rows), and then colour their cells themselves: kLine, kBox and kFillBox all go through gxPix, which writes the PIXEL only, so the matrix nibble that decides what colour the shape shows is written by a walk of the CELL grid, one kPlot per cell -- the outline's two edge columns and two edge rows, every row for the fill. The walk's state (GP_WK_*: the two edges, the two rows, the cell counts and the live counters) cannot live in kArg, because kPlot clobbers it between two cells. A held button cannot commit twice, which is why the LEFT button keeps a previous-frame byte (GP_PREV) while the right button deliberately does not need one. Only the display owner may open the palette (it reads the framebuffer directly), a stroke in progress blocks it for the PENCIL only (a shape tool draws nothing until its second click), and a left press closes the palette before it draws. The canvas's matrix is filled from the palette at alloc -- (ink << 4) | paper in hires, where a cell's colour IS its matrix nibbles. Every mark is then CLAIMED (gpClaim, step 4.15): in MC kPlot writes the pixel's 2-bit VALUE only and the colour it shows is the cell's own byte, so the mark gives the cell the pen's colour for that value (the high nibble for value 1, the low one for 2, the colour RAM for 3, nothing for 0 -- $D021 is one register) and keeps the cell's other colour, exactly as the hires mark keeps the paper. That is what makes a stroke come out in the pen's colour on a loaded picture, whose cells carry their own two colours; kPlot running first, the shape walks claiming through gpCellMark, and a whole-canvas fill (gpCells) writing the same byte are what keep the two in step. The flood fill claims no cell: kFloodFill reports no list of the cells it filled. The command line is 'run grpaint <base> [border paper ink | border bg c1 c2 c3]': the COUNT of the values is the mode (three hires, five MC, none for the black-and-white defaults, anything else refused), a picture that already exists is a load whose own mode and colours win and which therefore refuses a colour argument, and the base name is at most 7 characters with no dot. UNDO AND REDO (steps 4.9/4.10): a ring of six canvas states in the REU's top full bank, $FB0000-$FBFFFF (bank 251), which reuFsCapacity stops one bank short of the top to reserve, so the filesystem can never grow into it -- a machine with fewer than 252 banks has no such bank and gets no history at all (gpUndoCap refuses and nothing is written). A state is a .pic PAYLOAD (gpPicTab's three regions at a file's own offsets, so a snapshot is three DMAs of the canvas's addresses) plus the mode and the four palette colours; six states are five steps each way and the seventh push drops the oldest. The ring holds the states EITHER SIDE of the one on screen, which is what makes redo the same walk as undo. GP_U_HEAD is the state the canvas IS: every action that changes it ends in gpUndoPush (a stroke's release, the line's SECOND click, a shape's commit, the fill, a text entry put away with the caret off the canvas, and the CLEAR tile, an MC palette press and the MODE tile through gpUndoPushBand, which takes the band down for the snapshot because its own rows are the canvas's bottom two). The push stores the canvas after the state on screen, so a new action after an undo DISCARDS the redo tail, and a mark that is on the canvas but not in a state (the line's anchor click, a stroke still under the button) is taken off first by gpDropMark -- it is not a step, so it never enters the ring, and the press that took it off stops there (C=1), so a step never takes off the unfinished mark and the action before it together. A step restores the pixels, the cells, the mode and the palette, and sets GP_DIRTY -- and never the pen: the tool, the ink, the slot and the value are the user's, not the picture's. Every entry point is display-guarded, since a snapshot READS the framebuffer, the matrix and the colour RAM, which are the canvas only while it is the surface on screen. The gestures are u and r on the keyboard (the text pen has the keys while it is down) and the band's two arrows: undo across its TOP cell row and redo across the bottom one, in the last two columns -- 16x8 each and STACKED, because a full 16x16 icon would want four columns and only two are free after the tiles and the four slot chips, so the picker reads the band's ROW there rather than its column, and either arrow is MARKED WHITE while the ring has a step that way (HEAD against GP_U_OLD and GP_U_NEW, folded into gpMark so every path that re-reads the band's matrix marks them). A mark that no state holds -- the line's anchor click, a stroke still under the button -- is NOT a step: it colours no arrow, it never enters the ring, and it is taken off by gpDropMark, which shows HEAD again (HEAD does not contain it) with GP_DRAW cleared and C=1 so a step press stops there (one press one thing; the next press is the step). The band's own opening drops a LINE's waiting anchor before it reads the band, so the arrows always describe the ring, while the band comes back at the step's shared tail. Declares sprite.lib + mouse.lib + gfx.lib + filesys.lib ---
.var grpaintBase     = LoadBinary("../tasks-out/temp/grpaint.base.bin")
.var grpaintShift    = LoadBinary("../tasks-out/temp/grpaint.shift.bin")
.var grpaintCheck    = LoadBinary("../tasks-out/temp/grpaint.check.bin")
.var grpaintNameLen  = grpaintBase.get(12)
.var grpaintCodeSize = grpaintBase.getSize() - 256
.var grpaintFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < grpaintBase.getSize(); i++) {
    .if (((grpaintShift.get(i) & $FF) - (grpaintBase.get(i) & $FF)) == 1) {
        .eval grpaintFixups.add(i - 256)
    }
}
.print "grpaint: " + grpaintCodeSize + " bytes of code, " + grpaintFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < grpaintFixups.size(); f++) {
    .var idx = 256 + grpaintFixups.get(f)
    .if ((((grpaintBase.get(idx) & $FF) + 2) & $FF) != (grpaintCheck.get(idx) & $FF)) {
        .error "grpaint: fixup verify failed at code offset " + grpaintFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var grpaintFi = 0
.for (var i = 256; i < grpaintBase.getSize(); i++) {
    .if ((grpaintFi < grpaintFixups.size()) && (i - 256 == grpaintFixups.get(grpaintFi))) {
        .eval grpaintFi++
    } else {
        .if ((grpaintBase.get(i) & $FF) != (grpaintCheck.get(i) & $FF)) {
            .error "grpaint: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "grpaint: relocation verification OK"

.segmentdef grpaintFinal [outBin="../tasks-out/grpaint.bin"]
.segment grpaintFinal

// magic byte 1 (+0) is shared.
.fill 2, grpaintBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + grpaintNameLen, grpaintBase.get(2 + i)      // flags..name
    .word grpaintFixups.size()                          // fixup count (16-bit)
    .word 256 + grpaintCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + grpaintNameLen > 256) {
        .error "grpaint: header overflow"
    }
    .fill 256 - (17 + grpaintNameLen), 0                // pad to the 256-byte page
    .fill grpaintCodeSize, grpaintBase.get(256 + i)         // code body
    .for (var f = 0; f < grpaintFixups.size(); f++) {
        .word grpaintFixups.get(f)
    }
    .word $FFFF

// --- granim - KF_TAKES_ARGS - the REU animation film player: `run granim llama` opens llama.inf (the suffix appended in place, like grview's, so the base name is <= 7) and plays the chunks it names. The manifest carries the rate, the palette, the frame count, the chunk shape and the first chunk's name, so the task knows no particular film; the chunk number is bumped IN PLACE in that name, so no number is ever formatted on the machine. A chunk is a run of whole .pic records and a frame goes straight to $0400/$D800/$2000 in six kReuReadAt calls of at most 2000 bytes, and THE ORDER IS MATRIX, COLOUR RAM, THEN PIXELS TOP ROW DOWN -- that order is what makes a whole-screen rewrite tear-free, because the beam reads a line's matrix and colour before that line's pixels, so every write lands ahead of the beam instead of behind it (the frame does not fit the border window and does not need to: 10,000 cycles of DMA against a 12,600-cycle visible period, the beam consuming 40 bytes a line where this writes 63). The clock is frameCount, the display's own 50.1 Hz, never the scheduler tick, with a fractional accumulator whose step is the film's own rate -- so 24 fps alternates two and three display frames a film frame instead of drifting into a stutter. Losing the display to an F-key switch PAUSES the film rather than writing a frame into another task's surface, and it resumes from the next frame because the one on show is the one that was saved. 'q' exits, SPACE holds it, '+'/'-' change speed (the accumulator's step is the rate shifted); a failure takes a screen of its own and waits for a key, sidplay-style; declares gfx.lib + filesys.lib ---
.var granimBase     = LoadBinary("../tasks-out/temp/granim.base.bin")
.var granimShift    = LoadBinary("../tasks-out/temp/granim.shift.bin")
.var granimCheck    = LoadBinary("../tasks-out/temp/granim.check.bin")
.var granimNameLen  = granimBase.get(12)
.var granimCodeSize = granimBase.getSize() - 256
.var granimFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < granimBase.getSize(); i++) {
    .if (((granimShift.get(i) & $FF) - (granimBase.get(i) & $FF)) == 1) {
        .eval granimFixups.add(i - 256)
    }
}
.print "granim: " + granimCodeSize + " bytes of code, " + granimFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < granimFixups.size(); f++) {
    .var idx = 256 + granimFixups.get(f)
    .if ((((granimBase.get(idx) & $FF) + 2) & $FF) != (granimCheck.get(idx) & $FF)) {
        .error "granim: fixup verify failed at code offset " + granimFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var granimFi = 0
.for (var i = 256; i < granimBase.getSize(); i++) {
    .if ((granimFi < granimFixups.size()) && (i - 256 == granimFixups.get(granimFi))) {
        .eval granimFi++
    } else {
        .if ((granimBase.get(i) & $FF) != (granimCheck.get(i) & $FF)) {
            .error "granim: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "granim: relocation verification OK"

.segmentdef granimFinal [outBin="../tasks-out/granim.bin"]
.segment granimFinal

// magic byte 1 (+0) is shared.
.fill 2, granimBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + granimNameLen, granimBase.get(2 + i)      // flags..name
    .word granimFixups.size()                          // fixup count (16-bit)
    .word 256 + granimCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + granimNameLen > 256) {
        .error "granim: header overflow"
    }
    .fill 256 - (17 + granimNameLen), 0                // pad to the 256-byte page
    .fill granimCodeSize, granimBase.get(256 + i)         // code body
    .for (var f = 0; f < granimFixups.size(); f++) {
        .word granimFixups.get(f)
    }
    .word $FFFF

// --- timelib - lib 0 (eager), relocatable ---
.var timelibBase     = LoadBinary("../tasks-out/temp/timelib.base.bin")
.var timelibShift    = LoadBinary("../tasks-out/temp/timelib.shift.bin")
.var timelibCheck    = LoadBinary("../tasks-out/temp/timelib.check.bin")
.var timelibNameLen  = timelibBase.get(12)
.var timelibCodeSize = timelibBase.getSize() - 256
.var timelibFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < timelibBase.getSize(); i++) {
    .if (((timelibShift.get(i) & $FF) - (timelibBase.get(i) & $FF)) == 1) {
        .eval timelibFixups.add(i - 256)
    }
}
.print "timelib: " + timelibCodeSize + " bytes of code, " + timelibFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < timelibFixups.size(); f++) {
    .var idx = 256 + timelibFixups.get(f)
    .if ((((timelibBase.get(idx) & $FF) + 2) & $FF) != (timelibCheck.get(idx) & $FF)) {
        .error "timelib: fixup verify failed at code offset " + timelibFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var timelibFi = 0
.for (var i = 256; i < timelibBase.getSize(); i++) {
    .if ((timelibFi < timelibFixups.size()) && (i - 256 == timelibFixups.get(timelibFi))) {
        .eval timelibFi++
    } else {
        .if ((timelibBase.get(i) & $FF) != (timelibCheck.get(i) & $FF)) {
            .error "timelib: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "timelib: relocation verification OK"

.segmentdef timelibFinal [outBin="../tasks-out/timelib.bin"]
.segment timelibFinal

// magic byte 1 (+0) is shared.
.fill 2, timelibBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + timelibNameLen, timelibBase.get(2 + i)      // flags..name
    .word timelibFixups.size()                          // fixup count (16-bit)
    .word 256 + timelibCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + timelibNameLen > 256) {
        .error "timelib: header overflow"
    }
    .fill 256 - (17 + timelibNameLen), 0                // pad to the 256-byte page
    .fill timelibCodeSize, timelibBase.get(256 + i)         // code body
    .for (var f = 0; f < timelibFixups.size(); f++) {
        .word timelibFixups.get(f)
    }
    .word $FFFF

// --- filesyslib - lib 1 (lazy), relocatable ---
.var filesyslibBase     = LoadBinary("../tasks-out/temp/filesyslib.base.bin")
.var filesyslibShift    = LoadBinary("../tasks-out/temp/filesyslib.shift.bin")
.var filesyslibCheck    = LoadBinary("../tasks-out/temp/filesyslib.check.bin")
.var filesyslibNameLen  = filesyslibBase.get(12)
.var filesyslibCodeSize = filesyslibBase.getSize() - 256
.var filesyslibFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < filesyslibBase.getSize(); i++) {
    .if (((filesyslibShift.get(i) & $FF) - (filesyslibBase.get(i) & $FF)) == 1) {
        .eval filesyslibFixups.add(i - 256)
    }
}
.print "filesyslib: " + filesyslibCodeSize + " bytes of code, " + filesyslibFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < filesyslibFixups.size(); f++) {
    .var idx = 256 + filesyslibFixups.get(f)
    .if ((((filesyslibBase.get(idx) & $FF) + 2) & $FF) != (filesyslibCheck.get(idx) & $FF)) {
        .error "filesyslib: fixup verify failed at code offset " + filesyslibFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var filesyslibFi = 0
.for (var i = 256; i < filesyslibBase.getSize(); i++) {
    .if ((filesyslibFi < filesyslibFixups.size()) && (i - 256 == filesyslibFixups.get(filesyslibFi))) {
        .eval filesyslibFi++
    } else {
        .if ((filesyslibBase.get(i) & $FF) != (filesyslibCheck.get(i) & $FF)) {
            .error "filesyslib: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "filesyslib: relocation verification OK"

.segmentdef filesyslibFinal [outBin="../tasks-out/filesyslib.bin"]
.segment filesyslibFinal

// magic byte 1 (+0) is shared.
.fill 2, filesyslibBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + filesyslibNameLen, filesyslibBase.get(2 + i)      // flags..name
    .word filesyslibFixups.size()                          // fixup count (16-bit)
    .word 256 + filesyslibCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + filesyslibNameLen > 256) {
        .error "filesyslib: header overflow"
    }
    .fill 256 - (17 + filesyslibNameLen), 0                // pad to the 256-byte page
    .fill filesyslibCodeSize, filesyslibBase.get(256 + i)         // code body
    .for (var f = 0; f < filesyslibFixups.size(); f++) {
        .word filesyslibFixups.get(f)
    }
    .word $FFFF

// --- gfxlib - lib 2 (lazy), relocatable ---
.var gfxlibBase     = LoadBinary("../tasks-out/temp/gfxlib.base.bin")
.var gfxlibShift    = LoadBinary("../tasks-out/temp/gfxlib.shift.bin")
.var gfxlibCheck    = LoadBinary("../tasks-out/temp/gfxlib.check.bin")
.var gfxlibNameLen  = gfxlibBase.get(12)
.var gfxlibCodeSize = gfxlibBase.getSize() - 256
.var gfxlibFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < gfxlibBase.getSize(); i++) {
    .if (((gfxlibShift.get(i) & $FF) - (gfxlibBase.get(i) & $FF)) == 1) {
        .eval gfxlibFixups.add(i - 256)
    }
}
.print "gfxlib: " + gfxlibCodeSize + " bytes of code, " + gfxlibFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < gfxlibFixups.size(); f++) {
    .var idx = 256 + gfxlibFixups.get(f)
    .if ((((gfxlibBase.get(idx) & $FF) + 2) & $FF) != (gfxlibCheck.get(idx) & $FF)) {
        .error "gfxlib: fixup verify failed at code offset " + gfxlibFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var gfxlibFi = 0
.for (var i = 256; i < gfxlibBase.getSize(); i++) {
    .if ((gfxlibFi < gfxlibFixups.size()) && (i - 256 == gfxlibFixups.get(gfxlibFi))) {
        .eval gfxlibFi++
    } else {
        .if ((gfxlibBase.get(i) & $FF) != (gfxlibCheck.get(i) & $FF)) {
            .error "gfxlib: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "gfxlib: relocation verification OK"

.segmentdef gfxlibFinal [outBin="../tasks-out/gfxlib.bin"]
.segment gfxlibFinal

// magic byte 1 (+0) is shared.
.fill 2, gfxlibBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + gfxlibNameLen, gfxlibBase.get(2 + i)      // flags..name
    .word gfxlibFixups.size()                          // fixup count (16-bit)
    .word 256 + gfxlibCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + gfxlibNameLen > 256) {
        .error "gfxlib: header overflow"
    }
    .fill 256 - (17 + gfxlibNameLen), 0                // pad to the 256-byte page
    .fill gfxlibCodeSize, gfxlibBase.get(256 + i)         // code body
    .for (var f = 0; f < gfxlibFixups.size(); f++) {
        .word gfxlibFixups.get(f)
    }
    .word $FFFF

// --- fplib - lib 3 (eager), relocatable; note the out name is fp.bin, not fplib.bin ---
.var fplibBase     = LoadBinary("../tasks-out/temp/fplib.base.bin")
.var fplibShift    = LoadBinary("../tasks-out/temp/fplib.shift.bin")
.var fplibCheck    = LoadBinary("../tasks-out/temp/fplib.check.bin")
.var fplibNameLen  = fplibBase.get(12)
.var fplibCodeSize = fplibBase.getSize() - 256
.var fplibFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < fplibBase.getSize(); i++) {
    .if (((fplibShift.get(i) & $FF) - (fplibBase.get(i) & $FF)) == 1) {
        .eval fplibFixups.add(i - 256)
    }
}
.print "fplib: " + fplibCodeSize + " bytes of code, " + fplibFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < fplibFixups.size(); f++) {
    .var idx = 256 + fplibFixups.get(f)
    .if ((((fplibBase.get(idx) & $FF) + 2) & $FF) != (fplibCheck.get(idx) & $FF)) {
        .error "fplib: fixup verify failed at code offset " + fplibFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var fplibFi = 0
.for (var i = 256; i < fplibBase.getSize(); i++) {
    .if ((fplibFi < fplibFixups.size()) && (i - 256 == fplibFixups.get(fplibFi))) {
        .eval fplibFi++
    } else {
        .if ((fplibBase.get(i) & $FF) != (fplibCheck.get(i) & $FF)) {
            .error "fplib: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "fplib: relocation verification OK"

.segmentdef fplibFinal [outBin="../tasks-out/fp.bin"]
.segment fplibFinal

// magic byte 1 (+0) is shared.
.fill 2, fplibBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + fplibNameLen, fplibBase.get(2 + i)      // flags..name
    .word fplibFixups.size()                          // fixup count (16-bit)
    .word 256 + fplibCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + fplibNameLen > 256) {
        .error "fplib: header overflow"
    }
    .fill 256 - (17 + fplibNameLen), 0                // pad to the 256-byte page
    .fill fplibCodeSize, fplibBase.get(256 + i)         // code body
    .for (var f = 0; f < fplibFixups.size(); f++) {
        .word fplibFixups.get(f)
    }
    .word $FFFF

// --- stringlib - lib 4 (lazy), relocatable ---
.var stringlibBase     = LoadBinary("../tasks-out/temp/stringlib.base.bin")
.var stringlibShift    = LoadBinary("../tasks-out/temp/stringlib.shift.bin")
.var stringlibCheck    = LoadBinary("../tasks-out/temp/stringlib.check.bin")
.var stringlibNameLen  = stringlibBase.get(12)
.var stringlibCodeSize = stringlibBase.getSize() - 256
.var stringlibFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < stringlibBase.getSize(); i++) {
    .if (((stringlibShift.get(i) & $FF) - (stringlibBase.get(i) & $FF)) == 1) {
        .eval stringlibFixups.add(i - 256)
    }
}
.print "stringlib: " + stringlibCodeSize + " bytes of code, " + stringlibFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < stringlibFixups.size(); f++) {
    .var idx = 256 + stringlibFixups.get(f)
    .if ((((stringlibBase.get(idx) & $FF) + 2) & $FF) != (stringlibCheck.get(idx) & $FF)) {
        .error "stringlib: fixup verify failed at code offset " + stringlibFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var stringlibFi = 0
.for (var i = 256; i < stringlibBase.getSize(); i++) {
    .if ((stringlibFi < stringlibFixups.size()) && (i - 256 == stringlibFixups.get(stringlibFi))) {
        .eval stringlibFi++
    } else {
        .if ((stringlibBase.get(i) & $FF) != (stringlibCheck.get(i) & $FF)) {
            .error "stringlib: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "stringlib: relocation verification OK"

.segmentdef stringlibFinal [outBin="../tasks-out/stringlib.bin"]
.segment stringlibFinal

// magic byte 1 (+0) is shared.
.fill 2, stringlibBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + stringlibNameLen, stringlibBase.get(2 + i)      // flags..name
    .word stringlibFixups.size()                          // fixup count (16-bit)
    .word 256 + stringlibCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + stringlibNameLen > 256) {
        .error "stringlib: header overflow"
    }
    .fill 256 - (17 + stringlibNameLen), 0                // pad to the 256-byte page
    .fill stringlibCodeSize, stringlibBase.get(256 + i)         // code body
    .for (var f = 0; f < stringlibFixups.size(); f++) {
        .word stringlibFixups.get(f)
    }
    .word $FFFF

// --- sidlib - lib 5 (lazy), relocatable; the Sidplayer (sidplay.inc) behind kSidPlay/kSidHush; callers must declare ZP $02-$0A ---
.var sidlibBase     = LoadBinary("../tasks-out/temp/sidlib.base.bin")
.var sidlibShift    = LoadBinary("../tasks-out/temp/sidlib.shift.bin")
.var sidlibCheck    = LoadBinary("../tasks-out/temp/sidlib.check.bin")
.var sidlibNameLen  = sidlibBase.get(12)
.var sidlibCodeSize = sidlibBase.getSize() - 256
.var sidlibFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < sidlibBase.getSize(); i++) {
    .if (((sidlibShift.get(i) & $FF) - (sidlibBase.get(i) & $FF)) == 1) {
        .eval sidlibFixups.add(i - 256)
    }
}
.print "sidlib: " + sidlibCodeSize + " bytes of code, " + sidlibFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < sidlibFixups.size(); f++) {
    .var idx = 256 + sidlibFixups.get(f)
    .if ((((sidlibBase.get(idx) & $FF) + 2) & $FF) != (sidlibCheck.get(idx) & $FF)) {
        .error "sidlib: fixup verify failed at code offset " + sidlibFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var sidlibFi = 0
.for (var i = 256; i < sidlibBase.getSize(); i++) {
    .if ((sidlibFi < sidlibFixups.size()) && (i - 256 == sidlibFixups.get(sidlibFi))) {
        .eval sidlibFi++
    } else {
        .if ((sidlibBase.get(i) & $FF) != (sidlibCheck.get(i) & $FF)) {
            .error "sidlib: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "sidlib: relocation verification OK"

.segmentdef sidlibFinal [outBin="../tasks-out/sidlib.bin"]
.segment sidlibFinal

// magic byte 1 (+0) is shared.
.fill 2, sidlibBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + sidlibNameLen, sidlibBase.get(2 + i)      // flags..name
    .word sidlibFixups.size()                          // fixup count (16-bit)
    .word 256 + sidlibCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + sidlibNameLen > 256) {
        .error "sidlib: header overflow"
    }
    .fill 256 - (17 + sidlibNameLen), 0                // pad to the 256-byte page
    .fill sidlibCodeSize, sidlibBase.get(256 + i)         // code body
    .for (var f = 0; f < sidlibFixups.size(); f++) {
        .word sidlibFixups.get(f)
    }
    .word $FFFF

// --- ipclib - lib 6 (lazy), relocatable; IPC pipe (kIpcPost/kIpcPick) as a 128-byte ring in the library's own shared data, lock-free SPSC, no caller ZP ---
.var ipclibBase     = LoadBinary("../tasks-out/temp/ipclib.base.bin")
.var ipclibShift    = LoadBinary("../tasks-out/temp/ipclib.shift.bin")
.var ipclibCheck    = LoadBinary("../tasks-out/temp/ipclib.check.bin")
.var ipclibNameLen  = ipclibBase.get(12)
.var ipclibCodeSize = ipclibBase.getSize() - 256
.var ipclibFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < ipclibBase.getSize(); i++) {
    .if (((ipclibShift.get(i) & $FF) - (ipclibBase.get(i) & $FF)) == 1) {
        .eval ipclibFixups.add(i - 256)
    }
}
.print "ipclib: " + ipclibCodeSize + " bytes of code, " + ipclibFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < ipclibFixups.size(); f++) {
    .var idx = 256 + ipclibFixups.get(f)
    .if ((((ipclibBase.get(idx) & $FF) + 2) & $FF) != (ipclibCheck.get(idx) & $FF)) {
        .error "ipclib: fixup verify failed at code offset " + ipclibFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var ipclibFi = 0
.for (var i = 256; i < ipclibBase.getSize(); i++) {
    .if ((ipclibFi < ipclibFixups.size()) && (i - 256 == ipclibFixups.get(ipclibFi))) {
        .eval ipclibFi++
    } else {
        .if ((ipclibBase.get(i) & $FF) != (ipclibCheck.get(i) & $FF)) {
            .error "ipclib: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "ipclib: relocation verification OK"

.segmentdef ipclibFinal [outBin="../tasks-out/ipclib.bin"]
.segment ipclibFinal

// magic byte 1 (+0) is shared.
.fill 2, ipclibBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + ipclibNameLen, ipclibBase.get(2 + i)      // flags..name
    .word ipclibFixups.size()                          // fixup count (16-bit)
    .word 256 + ipclibCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + ipclibNameLen > 256) {
        .error "ipclib: header overflow"
    }
    .fill 256 - (17 + ipclibNameLen), 0                // pad to the 256-byte page
    .fill ipclibCodeSize, ipclibBase.get(256 + i)         // code body
    .for (var f = 0; f < ipclibFixups.size(); f++) {
        .word ipclibFixups.get(f)
    }
    .word $FFFF

// --- spritelib - lib 7 (eager), relocatable; the sprite API behind four call entries (sheet/frame/pos/reg) -- per-task sprite state, handed over with the display (docs/lib-sprite.md) ---
.var spritelibBase     = LoadBinary("../tasks-out/temp/spritelib.base.bin")
.var spritelibShift    = LoadBinary("../tasks-out/temp/spritelib.shift.bin")
.var spritelibCheck    = LoadBinary("../tasks-out/temp/spritelib.check.bin")
.var spritelibNameLen  = spritelibBase.get(12)
.var spritelibCodeSize = spritelibBase.getSize() - 256
.var spritelibFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < spritelibBase.getSize(); i++) {
    .if (((spritelibShift.get(i) & $FF) - (spritelibBase.get(i) & $FF)) == 1) {
        .eval spritelibFixups.add(i - 256)
    }
}
.print "spritelib: " + spritelibCodeSize + " bytes of code, " + spritelibFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < spritelibFixups.size(); f++) {
    .var idx = 256 + spritelibFixups.get(f)
    .if ((((spritelibBase.get(idx) & $FF) + 2) & $FF) != (spritelibCheck.get(idx) & $FF)) {
        .error "spritelib: fixup verify failed at code offset " + spritelibFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var spritelibFi = 0
.for (var i = 256; i < spritelibBase.getSize(); i++) {
    .if ((spritelibFi < spritelibFixups.size()) && (i - 256 == spritelibFixups.get(spritelibFi))) {
        .eval spritelibFi++
    } else {
        .if ((spritelibBase.get(i) & $FF) != (spritelibCheck.get(i) & $FF)) {
            .error "spritelib: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "spritelib: relocation verification OK"

.segmentdef spritelibFinal [outBin="../tasks-out/spritelib.bin"]
.segment spritelibFinal

// magic byte 1 (+0) is shared.
.fill 2, spritelibBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + spritelibNameLen, spritelibBase.get(2 + i)      // flags..name
    .word spritelibFixups.size()                          // fixup count (16-bit)
    .word 256 + spritelibCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + spritelibNameLen > 256) {
        .error "spritelib: header overflow"
    }
    .fill 256 - (17 + spritelibNameLen), 0                // pad to the 256-byte page
    .fill spritelibCodeSize, spritelibBase.get(256 + i)         // code body
    .for (var f = 0; f < spritelibFixups.size(); f++) {
        .word spritelibFixups.get(f)
    }
    .word $FFFF

// --- mouselib - lib 8 (eager), relocatable; the 1351 reader behind one dispatching entry (kMouseCall, call 41): MOUSE_READ publishes raw x/y and signed deltas in kArg+0..3 with the buttons in A, MOUSE_ENABLE primes, MOUSE_DISABLE refuses. Reads $D419/$D41A with the PRA $DC00 select and DDRA per bit under SEI, and never consults mouseKind (docs/lib-mouse.md) ---
.var mouselibBase     = LoadBinary("../tasks-out/temp/mouselib.base.bin")
.var mouselibShift    = LoadBinary("../tasks-out/temp/mouselib.shift.bin")
.var mouselibCheck    = LoadBinary("../tasks-out/temp/mouselib.check.bin")
.var mouselibNameLen  = mouselibBase.get(12)
.var mouselibCodeSize = mouselibBase.getSize() - 256
.var mouselibFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < mouselibBase.getSize(); i++) {
    .if (((mouselibShift.get(i) & $FF) - (mouselibBase.get(i) & $FF)) == 1) {
        .eval mouselibFixups.add(i - 256)
    }
}
.print "mouselib: " + mouselibCodeSize + " bytes of code, " + mouselibFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < mouselibFixups.size(); f++) {
    .var idx = 256 + mouselibFixups.get(f)
    .if ((((mouselibBase.get(idx) & $FF) + 2) & $FF) != (mouselibCheck.get(idx) & $FF)) {
        .error "mouselib: fixup verify failed at code offset " + mouselibFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var mouselibFi = 0
.for (var i = 256; i < mouselibBase.getSize(); i++) {
    .if ((mouselibFi < mouselibFixups.size()) && (i - 256 == mouselibFixups.get(mouselibFi))) {
        .eval mouselibFi++
    } else {
        .if ((mouselibBase.get(i) & $FF) != (mouselibCheck.get(i) & $FF)) {
            .error "mouselib: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "mouselib: relocation verification OK"

.segmentdef mouselibFinal [outBin="../tasks-out/mouselib.bin"]
.segment mouselibFinal

// magic byte 1 (+0) is shared.
.fill 2, mouselibBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + mouselibNameLen, mouselibBase.get(2 + i)      // flags..name
    .word mouselibFixups.size()                          // fixup count (16-bit)
    .word 256 + mouselibCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + mouselibNameLen > 256) {
        .error "mouselib: header overflow"
    }
    .fill 256 - (17 + mouselibNameLen), 0                // pad to the 256-byte page
    .fill mouselibCodeSize, mouselibBase.get(256 + i)         // code body
    .for (var f = 0; f < mouselibFixups.size(); f++) {
        .word mouselibFixups.get(f)
    }
    .word $FFFF

// --- mousetest - declares LIB_MOUSE - the 1351 protocol readout (docs/lib-mouse.md): reads $D419/$D41A directly, with the PRA $DC00 multiplexer select and DDRA per bit under SEI, and prints the raw POT byte, the position mod 64, the delta since the last sample, the running total and PA5..PA0 of $DC00 on the shell's SHARED screen, refreshed once every 4 displayed frames. 'l' takes the same sample through mouse.lib instead, 'p' switches control port and 'h' cycles the hold; any other key exits. It takes the keyboard focus with kSwitchView (the format pattern) so the key read works, and does not consult mouseKind, so it diagnoses a machine the OS believes has no mouse. fsName is 9 chars: the REU FS caps a filename at 11, and the typed command is the FS name, not `name` ---
.var mousetestBase     = LoadBinary("../tasks-out/temp/mousetest.base.bin")
.var mousetestShift    = LoadBinary("../tasks-out/temp/mousetest.shift.bin")
.var mousetestCheck    = LoadBinary("../tasks-out/temp/mousetest.check.bin")
.var mousetestNameLen  = mousetestBase.get(12)
.var mousetestCodeSize = mousetestBase.getSize() - 256
.var mousetestFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < mousetestBase.getSize(); i++) {
    .if (((mousetestShift.get(i) & $FF) - (mousetestBase.get(i) & $FF)) == 1) {
        .eval mousetestFixups.add(i - 256)
    }
}
.print "mousetest: " + mousetestCodeSize + " bytes of code, " + mousetestFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < mousetestFixups.size(); f++) {
    .var idx = 256 + mousetestFixups.get(f)
    .if ((((mousetestBase.get(idx) & $FF) + 2) & $FF) != (mousetestCheck.get(idx) & $FF)) {
        .error "mousetest: fixup verify failed at code offset " + mousetestFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var mousetestFi = 0
.for (var i = 256; i < mousetestBase.getSize(); i++) {
    .if ((mousetestFi < mousetestFixups.size()) && (i - 256 == mousetestFixups.get(mousetestFi))) {
        .eval mousetestFi++
    } else {
        .if ((mousetestBase.get(i) & $FF) != (mousetestCheck.get(i) & $FF)) {
            .error "mousetest: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "mousetest: relocation verification OK"

.segmentdef mousetestFinal [outBin="../tasks-out/mousetest.bin"]
.segment mousetestFinal

// magic byte 1 (+0) is shared.
.fill 2, mousetestBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + mousetestNameLen, mousetestBase.get(2 + i)      // flags..name
    .word mousetestFixups.size()                          // fixup count (16-bit)
    .word 256 + mousetestCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + mousetestNameLen > 256) {
        .error "mousetest: header overflow"
    }
    .fill 256 - (17 + mousetestNameLen), 0                // pad to the 256-byte page
    .fill mousetestCodeSize, mousetestBase.get(256 + i)         // code body
    .for (var f = 0; f < mousetestFixups.size(); f++) {
        .word mousetestFixups.get(f)
    }
    .word $FFFF

// --- mouse - declares the pointer device: 'mouse 1351' / 'mouse off' write the kernel's mouseKind byte ($E530, 0 = off, 1 = 1351), and bare 'mouse' prints the current setting ('? mouse 1351 off' for an unrecognised argument, with the byte untouched). Policy only -- no libraries, no hardware, silent on success; a machine that has a 1351 declares it with a line in boot.bat, which the shipped image carries (docs/lib-mouse.md) ---
.var mouseBase     = LoadBinary("../tasks-out/temp/mouse.base.bin")
.var mouseShift    = LoadBinary("../tasks-out/temp/mouse.shift.bin")
.var mouseCheck    = LoadBinary("../tasks-out/temp/mouse.check.bin")
.var mouseNameLen  = mouseBase.get(12)
.var mouseCodeSize = mouseBase.getSize() - 256
.var mouseFixups   = List()

// scan: a code byte that advanced by exactly one page is a high byte.
// Mask both operands to 0..255 - LoadBinary returns signed bytes, and
// an unmasked difference is only correct while no byte crosses $80.
.for (var i = 256; i < mouseBase.getSize(); i++) {
    .if (((mouseShift.get(i) & $FF) - (mouseBase.get(i) & $FF)) == 1) {
        .eval mouseFixups.add(i - 256)
    }
}
.print "mouse: " + mouseCodeSize + " bytes of code, " + mouseFixups.size() + " fixups"

// verify fixups: base + 2 pages must equal check.
.for (var f = 0; f < mouseFixups.size(); f++) {
    .var idx = 256 + mouseFixups.get(f)
    .if ((((mouseBase.get(idx) & $FF) + 2) & $FF) != (mouseCheck.get(idx) & $FF)) {
        .error "mouse: fixup verify failed at code offset " + mouseFixups.get(f)
    }
}

// verify position independence: every NON-fixup byte must be equal.
.var mouseFi = 0
.for (var i = 256; i < mouseBase.getSize(); i++) {
    .if ((mouseFi < mouseFixups.size()) && (i - 256 == mouseFixups.get(mouseFi))) {
        .eval mouseFi++
    } else {
        .if ((mouseBase.get(i) & $FF) != (mouseCheck.get(i) & $FF)) {
            .error "mouse: non-fixup byte changed at code offset " + (i - 256)
        }
    }
}
.print "mouse: relocation verification OK"

.segmentdef mouseFinal [outBin="../tasks-out/mouse.bin"]
.segment mouseFinal

// magic byte 1 (+0) is shared.
.fill 2, mouseBase.get(i)

// Header: flags..name, then the fixup trailer (16-bit count + the
// table offset), then zero padding to the 256-byte page.
    .fill 11 + mouseNameLen, mouseBase.get(2 + i)      // flags..name
    .word mouseFixups.size()                          // fixup count (16-bit)
    .word 256 + mouseCodeSize                         // fixup_offset (file offset of the table)
    .if (17 + mouseNameLen > 256) {
        .error "mouse: header overflow"
    }
    .fill 256 - (17 + mouseNameLen), 0                // pad to the 256-byte page
    .fill mouseCodeSize, mouseBase.get(256 + i)         // code body
    .for (var f = 0; f < mouseFixups.size(); f++) {
        .word mouseFixups.get(f)
    }
    .word $FFFF
