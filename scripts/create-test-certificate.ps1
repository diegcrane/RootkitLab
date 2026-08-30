[CmdletBinding()]
param(
    [string]$Subject = 'CN=RootkitLab Academic Test Certificate',
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\out\cert')
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$existing = Get-ChildItem Cert:\LocalMachine\My | Where-Object Subject -eq $Subject | Sort-Object NotAfter -Descending | Select-Object -First 1
if ($null -eq $existing) {
    $existing = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject -CertStoreLocation Cert:\LocalMachine\My -KeyExportPolicy Exportable -KeySpec Signature -KeyLength 2048 -HashAlgorithm sha256 -NotAfter (Get-Date).AddYears(2)
}
$cer = Join-Path $OutputDirectory 'RootkitLab-Test.cer'
Export-Certificate -Cert $existing -FilePath $cer -Force | Out-Null
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
Import-Certificate -FilePath $cer -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
[pscustomobject]@{ Thumbprint=$existing.Thumbprint; Subject=$existing.Subject; NotAfter=$existing.NotAfter; Cer=$cer }
