function Log-Debug {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory,ValueFromPipeline)]
        [ValidateNotNullOrEmpty()]
        [string[]] $Message
    )

    Process {
        foreach($m in $Message) {
            Write-Debug "$(if ( $env:CI -ne $null ) { '::debug::' })$m"
        }
    }
}

function Log-Verbose {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory,ValueFromPipeline)]
        [ValidateNotNullOrEmpty()]
        [string[]] $Message
    )

    Process {
        foreach($m in $Message) {
            Write-Verbose $m
        }
    }
}

function Log-Warning {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory,ValueFromPipeline)]
        [ValidateNotNullOrEmpty()]
        [string[]] $Message
    )

    Process {
        foreach($m in $Message) {
            Write-Warning "$(if ( $env:CI -ne $null ) { '::warning::' })$m"
        }
    }
}

function Log-Error {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory,ValueFromPipeline)]
        [ValidateNotNullOrEmpty()]
        [string[]] $Message
    )

    Process {
        foreach($m in $Message) {
            Write-Error "$(if ( $env:CI -ne $null ) { '::error::' })$m"
        }
    }
}

function Log-Information {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory,ValueFromPipeline)]
        [ValidateNotNullOrEmpty()]
        [string[]] $Message
    )

    Process {
        if ( ! ( $script:Quiet ) ) {
            $StageName = $( if ( $script:StageName -ne $null ) { $script:StageName } else { '' })
            $Icon = ' =>'

            foreach($m in $Message) {
                Write-Host -NoNewLine -ForegroundColor Blue "  ${StageName} $($Icon.PadRight(5)) "
                Write-Host "${m}"
            }
        }
    }
}

function Log-Group {
    [CmdletBinding()]
    param(
        [Parameter(ValueFromPipeline)]
        [string[]] $Message
    )

    Process {
        if ( $env:CI -ne $null ) {
            if ( $script:LogGroup ) {
                Write-Output '::endgroup::'
                $script:LogGroup = $false
            }

            if ( $Message.count -ge 1 ) {
                Write-Output "::group::$($Message -join ' ')"
                $script:LogGroup = $true
            }
        } else {
            if ( $Message.count -ge 1 ) {
                Log-Information $Message
            }
        }
    }
}

function Log-Status {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory,ValueFromPipeline)]
        [ValidateNotNullOrEmpty()]
        [string[]] $Message
    )

    Process {
        if ( ! ( $script:Quiet ) ) {
            $StageName = $( if ( $script:StageName -ne $null ) { $script:StageName } else { '' })
            $Icon = '  >'

            foreach($m in $Message) {
                Write-Host -NoNewLine -ForegroundColor Green "  ${StageName} $($Icon.PadRight(5)) "
                Write-Host "${m}"
            }
        }
    }
}

function Log-Output {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory,ValueFromPipeline)]
        [ValidateNotNullOrEmpty()]
        [string[]] $Message
    )

    Process {
        if ( ! ( $script:Quiet ) ) {
            $StageName = $( if ( $script:StageName -ne $null ) { $script:StageName } else { '' })
            $Icon = ''

            foreach($m in $Message) {
                Write-Output "  ${StageName} $($Icon.PadRight(5)) ${m}"
            }
        }
    }
}

$Columns = try { (Get-Host).UI.RawUI.WindowSize.Width - 5 } catch { 80 }
