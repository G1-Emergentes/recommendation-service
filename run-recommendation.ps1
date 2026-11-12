param([switch]$Demo)
$ErrorActionPreference = 'Stop'
$config = Join-Path $PSScriptRoot '.env'
if (Test-Path -LiteralPath $config) {
    foreach ($line in Get-Content -LiteralPath $config) {
        if ($line -match '^\s*(RECOMMENDATION_MODE|RECOMMENDATION_ENGINE|RECOMMENDATION_PORT|DIGITAL_CLOSET_BASE_URL|DIGITAL_CLOSET_TIMEOUT_SECONDS|IAM_BASE_URL|IAM_TIMEOUT_SECONDS|IAM_SUPABASE_URL|RECOMMENDATION_SUPABASE_URL|RECOMMENDATION_SUPABASE_SECRET_KEY|NVIDIA_API_KEY|NVIDIA_MODEL|NVIDIA_API_ORIGIN|NVIDIA_TIMEOUT_SECONDS|NVIDIA_EMBEDDING_API_KEY|NVIDIA_EMBEDDING_MODEL|NVIDIA_EMBEDDING_DIMENSIONS|NVIDIA_EMBEDDING_TIMEOUT_SECONDS|NEON_VECTOR_DATABASE_URL|NEON_VECTOR_PROJECT_ID|NEON_VECTOR_BRANCH_ID|RECOMMENDATION_RAG_ENABLED|NVIDIA_EMBEDDING_API_ORIGIN|FASHION_WEB_ORIGIN|FASHION_WEB_TIMEOUT_SECONDS)\s*=\s*([^#]+?)\s*$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2].Trim(), 'Process')
        }
    }
}
if ($Demo) { $env:RECOMMENDATION_MODE = 'demo' }
$executable = Join-Path $PSScriptRoot 'out\build\x64-debug\recommendation-service.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw 'Compile x64-debug first. See README.md.'
}
& $executable
exit $LASTEXITCODE
