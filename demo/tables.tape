# Records docs/tables.png (magips -s) against the fake demo machine: `make gif`
Output build/tables.gif
Require magips

Set Shell "bash"
Set FontSize 15
Set Width 1000
Set Height 1060
Set Padding 18
Set Theme "Catppuccin Mocha"

Hide
Type "source build/demo/env.sh && clear" Enter
Sleep 500ms
Show
Type "magips -s" Enter
Sleep 1s
Screenshot docs/tables.png
