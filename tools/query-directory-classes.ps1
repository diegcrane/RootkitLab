[CmdletBinding()]
param(
    [string]$RootkitLabExe = 'C:\TFM\RootkitLab-v2-demo\RootkitLab.exe',
    [string]$Sandbox = 'C:\RootkitLabSandbox',
    [string]$OutputPath = 'C:\TFM\RootkitLab-v2-demo\class-coverage.json'
)

$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class NativeDirectoryProbe
{
    [StructLayout(LayoutKind.Sequential)]
    private struct IO_STATUS_BLOCK
    {
        public IntPtr Status;
        public UIntPtr Information;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(
        string fileName,
        uint desiredAccess,
        uint shareMode,
        IntPtr securityAttributes,
        uint creationDisposition,
        uint flagsAndAttributes,
        IntPtr templateFile);

    [DllImport("ntdll.dll")]
    private static extern int NtQueryDirectoryFile(
        SafeFileHandle fileHandle,
        IntPtr eventHandle,
        IntPtr apcRoutine,
        IntPtr apcContext,
        out IO_STATUS_BLOCK ioStatusBlock,
        IntPtr fileInformation,
        uint length,
        int fileInformationClass,
        [MarshalAs(UnmanagedType.U1)] bool returnSingleEntry,
        IntPtr fileName,
        [MarshalAs(UnmanagedType.U1)] bool restartScan);

    public static string[] Enumerate(string path, int informationClass,
        int fileNameLengthOffset, int fileNameOffset)
    {
        const uint FILE_LIST_DIRECTORY = 0x0001;
        const uint FILE_SHARE_ALL = 0x00000007;
        const uint OPEN_EXISTING = 3;
        const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
        const int STATUS_NO_MORE_FILES = unchecked((int)0x80000006);
        const int STATUS_BUFFER_OVERFLOW = unchecked((int)0x80000005);
        const int BufferSize = 65536;

        using (SafeFileHandle handle = CreateFileW(path, FILE_LIST_DIRECTORY,
            FILE_SHARE_ALL, IntPtr.Zero, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero))
        {
            if (handle.IsInvalid)
                throw new Win32Exception(Marshal.GetLastWin32Error());

            IntPtr buffer = Marshal.AllocHGlobal(BufferSize);
            try
            {
                var names = new List<string>();
                bool restart = true;
                while (true)
                {
                    IO_STATUS_BLOCK iosb;
                    int status = NtQueryDirectoryFile(handle, IntPtr.Zero,
                        IntPtr.Zero, IntPtr.Zero, out iosb, buffer, BufferSize,
                        informationClass, false, IntPtr.Zero, restart);
                    restart = false;

                    if (status == STATUS_NO_MORE_FILES)
                        break;
                    if (status != 0 && status != STATUS_BUFFER_OVERFLOW)
                        throw new InvalidOperationException(
                            "NtQueryDirectoryFile failed: 0x" + status.ToString("X8"));

                    ulong byteCount = iosb.Information.ToUInt64();
                    if (byteCount == 0 || byteCount > BufferSize)
                        throw new InvalidOperationException("Invalid byte count.");

                    int offset = 0;
                    while ((ulong)offset < byteCount)
                    {
                        if ((ulong)(offset + fileNameOffset) > byteCount)
                            throw new InvalidOperationException("Truncated record.");
                        int next = Marshal.ReadInt32(buffer, offset);
                        int nameBytes = Marshal.ReadInt32(buffer,
                            offset + fileNameLengthOffset);
                        if (nameBytes < 0 || (nameBytes & 1) != 0 ||
                            (ulong)(offset + fileNameOffset + nameBytes) > byteCount)
                            throw new InvalidOperationException("Invalid name length.");

                        string name = Marshal.PtrToStringUni(
                            IntPtr.Add(buffer, offset + fileNameOffset),
                            nameBytes / 2);
                        if (name != "." && name != "..")
                            names.Add(name);

                        if (next == 0)
                            break;
                        if (next < fileNameOffset || (next & 3) != 0 ||
                            (ulong)(offset + next) >= byteCount)
                            throw new InvalidOperationException("Invalid next offset.");
                        offset += next;
                    }
                }

                names.Sort(StringComparer.OrdinalIgnoreCase);
                return names.ToArray();
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
    }
}
'@

function Invoke-RootkitLab {
    param([string[]]$Arguments)
    $process = Start-Process -FilePath $RootkitLabExe -ArgumentList $Arguments `
        -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "RootkitLab failed with exit code $($process.ExitCode): $Arguments"
    }
}

$classes = @(
    [pscustomobject]@{ Name='FileDirectoryInformation';       Value=1;  NameLength=60; NameOffset=64 },
    [pscustomobject]@{ Name='FileFullDirectoryInformation';   Value=2;  NameLength=60; NameOffset=68 },
    [pscustomobject]@{ Name='FileBothDirectoryInformation';   Value=3;  NameLength=60; NameOffset=94 },
    [pscustomobject]@{ Name='FileNamesInformation';           Value=12; NameLength=8;  NameOffset=12 },
    [pscustomobject]@{ Name='FileIdBothDirectoryInformation'; Value=37; NameLength=60; NameOffset=104 },
    [pscustomobject]@{ Name='FileIdFullDirectoryInformation'; Value=38; NameLength=60; NameOffset=80 }
)

$hidden = @('presupuesto_2026.xlsx', 'proyecto_confidencial.txt')
$expected = @('.rootkitlab-lab', 'contrato_cliente.pdf', 'notas_reunion.txt',
    'presupuesto_2026.xlsx', 'proyecto_confidencial.txt', 'resumen_publico.txt')

try {
    Invoke-RootkitLab @('--disable')
    Invoke-RootkitLab @('--set-rules')
    $baseline = @()
    foreach ($class in $classes) {
        $entries = [NativeDirectoryProbe]::Enumerate($Sandbox, $class.Value,
            $class.NameLength, $class.NameOffset)
        $baseline += [pscustomobject]@{ Class=$class.Name; Value=$class.Value; Entries=$entries }
    }

    Invoke-RootkitLab @('--set-rules', $hidden[0], $hidden[1])
    Invoke-RootkitLab @('--enable')
    $active = @()
    foreach ($class in $classes) {
        $entries = [NativeDirectoryProbe]::Enumerate($Sandbox, $class.Value,
            $class.NameLength, $class.NameOffset)
        $active += [pscustomobject]@{ Class=$class.Name; Value=$class.Value; Entries=$entries }
    }

    $expectedVisible = @($expected | Where-Object { $_ -notin $hidden })
    $checks = foreach ($class in $classes) {
        $before = @($baseline | Where-Object Class -eq $class.Name).Entries
        $after = @($active | Where-Object Class -eq $class.Name).Entries
        [pscustomobject]@{
            Class = $class.Name
            BaselineCount = $before.Count
            ActiveCount = $after.Count
            BaselineExact = (@(Compare-Object $expected $before).Count -eq 0)
            ActiveExact = (@(Compare-Object $expectedVisible $after).Count -eq 0)
            HiddenAbsent = (@($hidden | Where-Object { $_ -in $after }).Count -eq 0)
        }
    }

    $result = [ordered]@{
        Schema = 'rootkitlab-directory-class-coverage-v2.0'
        GeneratedUtc = (Get-Date).ToUniversalTime().ToString('o')
        Sandbox = $Sandbox
        HiddenNames = $hidden
        Baseline = $baseline
        Active = $active
        Checks = $checks
        Passed = (@($checks | Where-Object { -not ($_.BaselineExact -and $_.ActiveExact -and $_.HiddenAbsent) }).Count -eq 0)
    }
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath -Encoding UTF8
    $result
}
finally {
    Invoke-RootkitLab @('--disable')
    Invoke-RootkitLab @('--set-rules')
}
