// ==========================================
//  header-offsets-probe.inc -- fixture for tools/check-header-offsets.ps1
// ==========================================
//  NOT part of any build, and not imported by anything: it exists only to be
//  scanned by the tripwire's negative test, which must exit 1 and name both
//  violating lines below. A check that cannot be shown to fail is not a check.
//
//  Run it with:
//    powershell -File tools/check-header-offsets.ps1 -Path tools/probes
//
//  The four lines carry one case each: two that MUST be flagged (a bare header
//  offset on each header-staging buffer) and two that must NOT be (the REU FS
//  directory's own literals, and the named form).

    lda reuDirBuf+6             // MUST be flagged -- +6 is HDR_CODE_SIZE's hi half
    lda scratchBuf+9            // MUST be flagged -- should be HDR_ZP_LEN
    lda reuDirBuf+17,y          // must NOT be flagged -- REU FS directory entry field
    lda reuDirBuf+1             // must NOT be flagged -- REU FS directory scratch byte
    lda reuDirBuf+HDR_CODE_SIZE // must NOT be flagged -- read by name
