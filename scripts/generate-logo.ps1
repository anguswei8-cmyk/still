$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -eq 'Core') {
    & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath
    exit $LASTEXITCODE
}
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System.Drawing;
using System.Drawing.Drawing2D;
public static class StillLogo {
    static GraphicsPath Rounded(float x, float y, float w, float h, float r) {
        var path = new GraphicsPath();
        path.AddArc(x, y, r*2, r*2, 180, 90); path.AddArc(x+w-r*2, y, r*2, r*2, 270, 90);
        path.AddArc(x+w-r*2, y+h-r*2, r*2, r*2, 0, 90); path.AddArc(x, y+h-r*2, r*2, r*2, 90, 90); path.CloseFigure();
        return path;
    }
    public static void Render(string file, int size) {
        using (var image = new Bitmap(size, size)) using (var g = Graphics.FromImage(image)) {
            g.SmoothingMode = SmoothingMode.AntiAlias; g.Clear(Color.Transparent); g.ScaleTransform(size/512f, size/512f);
            using (var blue = new SolidBrush(ColorTranslator.FromHtml("#454b57"))) using (var white = new SolidBrush(Color.White))
            using (var body = Rounded(0, 0, 512, 512, 112)) using (var sheet = Rounded(218, 218, 156, 160, 24))
            using (var frame = new GraphicsPath()) using (var corner = new GraphicsPath())
            using (var pen = new Pen(Color.White, 32)) using (var mint = new Pen(ColorTranslator.FromHtml("#d9e3f5"), 32))
            using (var line = new Pen(blue, 16)) {
                pen.StartCap = pen.EndCap = mint.StartCap = mint.EndCap = line.StartCap = line.EndCap = LineCap.Round;
                g.FillPath(blue, body);
                frame.AddLine(288, 138, 162, 138); frame.AddArc(138, 138, 48, 48, 270, -90); frame.AddLine(138, 162, 138, 306);
                frame.AddArc(138, 282, 48, 48, 180, -90); frame.AddLine(162, 330, 182, 330); g.DrawPath(pen, frame);
                corner.AddLine(342, 138, 350, 138); corner.AddArc(326, 138, 48, 48, 270, 90); corner.AddLine(374, 162, 374, 174); g.DrawPath(mint, corner);
                g.FillPath(white, sheet); g.DrawLine(line, 254, 268, 338, 268); g.DrawLine(line, 254, 304, 308, 304);
            }
            image.Save(file, System.Drawing.Imaging.ImageFormat.Png);
        }
    }
}
'@
$assetDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../assets'))
$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = @()
foreach ($size in $sizes) {
    $temporary = Join-Path $assetDirectory "icon-$size.tmp.png"
    [StillLogo]::Render($temporary, $size)
    $images += ,([IO.File]::ReadAllBytes($temporary))
    Remove-Item -LiteralPath $temporary
}
$output = [IO.File]::Create((Join-Path $assetDirectory 'still.ico'))
$writer = [IO.BinaryWriter]::new($output)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; $index++) {
        $dimension = if ($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension); $writer.Write([uint16]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$images[$index].Length); $writer.Write([uint32]$offset)
        $offset += $images[$index].Length
    }
    foreach ($bytes in $images) { $writer.Write([byte[]]$bytes) }
} finally { $writer.Dispose(); $output.Dispose() }
[StillLogo]::Render((Join-Path $assetDirectory 'still.png'), 512)
$large = [IO.File]::ReadAllBytes((Join-Path $assetDirectory 'still.png'))
$output = [IO.File]::Create((Join-Path $assetDirectory 'still.icns'))
$writer = [IO.BinaryWriter]::new($output)
try {
    $writer.Write([Text.Encoding]::ASCII.GetBytes('icns'))
    $writer.Write([Net.IPAddress]::HostToNetworkOrder([int](24 + $images[6].Length + $large.Length)))
    $writer.Write([Text.Encoding]::ASCII.GetBytes('ic08')); $writer.Write([Net.IPAddress]::HostToNetworkOrder([int](8 + $images[6].Length))); $writer.Write([byte[]]$images[6])
    $writer.Write([Text.Encoding]::ASCII.GetBytes('ic09')); $writer.Write([Net.IPAddress]::HostToNetworkOrder([int](8 + $large.Length))); $writer.Write([byte[]]$large)
} finally { $writer.Dispose(); $output.Dispose() }
Write-Output 'Generated Still PNG, multi-size ICO and ICNS from the authored SVG geometry.'
