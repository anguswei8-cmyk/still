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
    static GraphicsPath Mark() {
        var path = new GraphicsPath();
        path.AddLine(338,206,286,206);
        path.AddBezier(286,206,284,187,273,177,253,177);
        path.AddBezier(253,177,231,177,217,187,217,202);
        path.AddBezier(217,202,217,218,231,224,263,232);
        path.AddBezier(263,232,314,244,340,262,340,302);
        path.AddBezier(340,302,340,348,305,376,254,376);
        path.AddBezier(254,376,201,376,169,350,166,305);
        path.AddLine(166,305,219,305);
        path.AddBezier(219,305,221,328,235,340,256,340);
        path.AddBezier(256,340,277,340,289,329,289,314);
        path.AddBezier(289,314,289,297,274,290,241,282);
        path.AddBezier(241,282,194,271,166,252,166,212);
        path.AddBezier(166,212,166,169,200,141,252,141);
        path.AddBezier(252,141,303,141,335,166,338,206);
        path.CloseFigure();
        return path;
    }
    static GraphicsPath Frame() {
        var path = new GraphicsPath();
        path.AddLine(168,108,132,108); path.AddBezier(132,108,116,108,108,116,108,132); path.AddLine(108,132,108,190);
        path.StartFigure();
        path.AddLine(344,108,380,108); path.AddBezier(380,108,396,108,404,116,404,132); path.AddLine(404,132,404,190);
        path.StartFigure();
        path.AddLine(108,322,108,380); path.AddBezier(108,380,108,396,116,404,132,404); path.AddLine(132,404,168,404);
        path.StartFigure();
        path.AddLine(404,322,404,380); path.AddBezier(404,380,404,396,396,404,380,404); path.AddLine(380,404,344,404);
        return path;
    }
    public static void Render(string file, int size, bool tray = false) {
        using (var image = new Bitmap(size, size)) using (var g = Graphics.FromImage(image))
        using (var mark = Mark()) using (var frame = Frame()) {
            g.SmoothingMode = SmoothingMode.AntiAlias; g.Clear(Color.Transparent);
            g.ScaleTransform(size/(tray ? 352f : 512f), size/(tray ? 352f : 512f));
            if (tray) {
                g.TranslateTransform(-80, -80);
                using (var pen = new Pen(Color.Black, 28)) { pen.LineJoin = LineJoin.Round; g.DrawPath(pen, frame); }
                g.FillPath(Brushes.Black, mark);
            } else {
                using (var baseShape = Rounded(16,20,480,480,108))
                using (var body = Rounded(16,16,480,480,108))
                using (var rim = Rounded(21,21,470,470,103))
                using (var dark = new SolidBrush(ColorTranslator.FromHtml("#1c2027")))
                using (var bodyBrush = new LinearGradientBrush(new PointF(16,16),new PointF(304,496),ColorTranslator.FromHtml("#626b78"),ColorTranslator.FromHtml("#252a33")))
                using (var silver = new LinearGradientBrush(new PointF(0,108),new PointF(0,404),Color.White,ColorTranslator.FromHtml("#d5dce5")))
                using (var rimBrush = new LinearGradientBrush(new PointF(0,21),new PointF(0,491),ColorTranslator.FromHtml("#aab2be"),ColorTranslator.FromHtml("#454d5b")))
                using (var rimPen = new Pen(rimBrush,3)) using (var pen = new Pen(silver,18))
                using (var shadow = new SolidBrush(ColorTranslator.FromHtml("#161a21"))) using (var shadowPen = new Pen(shadow,18)) {
                    pen.LineJoin = shadowPen.LineJoin = LineJoin.Round;
                    g.FillPath(dark,baseShape); g.FillPath(bodyBrush,body); g.DrawPath(rimPen,rim);
                    g.TranslateTransform(0,5); g.DrawPath(shadowPen,frame); g.FillPath(shadow,mark); g.TranslateTransform(0,-5);
                    g.DrawPath(pen,frame); g.FillPath(silver,mark);
                }
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
[StillLogo]::Render((Join-Path $assetDirectory 'still-tray.png'), 44, $true)
Write-Output 'Generated Still PNG, multi-size ICO, ICNS and monochrome tray template from the authored SVG geometry.'
