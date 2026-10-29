Option Explicit
' Import into Tools > Macro > New VBA module in SolidWorks.
' Requires installed SolidWorks; not executed in the generation environment.
Sub main()
    Dim sw As Object, doc As Object, files As Variant, n As Variant
    Dim folder As String, err As Long, warn As Long, ok As Boolean
    Set sw = Application.SldWorks
    folder = InputBox("Full path to extracted step folder (without final slash):")
    If Len(folder) = 0 Then Exit Sub
    files = Array("base", "lid", "electronics_tray")
    For Each n In files
        If Dir(folder & "\" & n & ".step") = "" Then
            MsgBox "Missing STEP: " & n
            Exit Sub
        End If
        Set doc = sw.LoadFile4(folder & "\" & n & ".step", "r", Nothing, err)
        If doc Is Nothing Then
            MsgBox "Import failed: " & n & "; error " & err
            Exit Sub
        End If
        doc.ViewZoomtofit2
        ok = doc.Extension.SaveAs(folder & "\" & n & ".SLDPRT", 0, 1, Nothing, err, warn)
        If Not ok Then
            MsgBox "Save failed: " & n & "; error " & err
            Exit Sub
        End If
    Next n
    MsgBox "Parts saved. Inspect imports and assemble at common origin."
End Sub
