# tasks-stamp.ps1 - the task build's input hash, shared by its writer and its checkers.
#
# tasks-out/ is build state that survives between builds, so nothing in it says
# which source state produced it: edit a task, forget the rebuild, run
# build-reu.ps1, and the image is seeded with the old binary -- while every other
# check still passes, because the image really does contain exactly those bytes.
# The stamp is a content hash of the inputs the task/library assembly reads:
# tools/tasks.json plus every .asm and .inc under src/ (a task pulls shared
# files in with `.import source`, so the manifest's own `source` list is not the
# whole input set). That makes "were these artifacts built from the sources on
# disk?" a mechanical question.
#
#   tools/build-tasks.ps1     writes tasks-out/build.stamp on success
#   tools/build-reu.ps1       refuses to seed when the stamp does not match
#   tools/check-reu-current.ps1  reports the same thing without seeding
function Get-TaskInputHash {
    $files = @(Get-ChildItem -Path 'src' -Recurse -File -Include '*.asm', '*.inc')
    $files += Get-Item 'tools/tasks.json'
    $lines = $files | Sort-Object FullName | ForEach-Object {
        '{0} {1}' -f $_.FullName, (Get-FileHash $_.FullName -Algorithm SHA256).Hash
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($lines -join "`n")))
    } finally {
        $sha.Dispose()
    }
    ($bytes | ForEach-Object { $_.ToString('x2') }) -join ''
}
