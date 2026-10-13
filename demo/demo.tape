# Records docs/demo.gif against the fake demo machine: `make gif`
Output docs/demo.gif
Require magips

Set Shell "bash"
Set FontSize 16
Set Width 1150
Set Height 560
Set Padding 18
Set Theme "Catppuccin Mocha"
Set TypingSpeed 90ms

Hide
Type "source build/demo/env.sh && clear" Enter
Sleep 500ms
Show

Type "magips" Sleep 400ms Enter
Sleep 2s
Type "j" Sleep 500ms
Type "j" Sleep 500ms
Type "k" Sleep 900ms

# sections: Apps, Services, Dev
Type "l" Sleep 1.2s
Type "l" Sleep 1.2s
Type "l" Sleep 1.8s

# stop the forgotten API server
Type "j" Sleep 900ms
Type "d" Sleep 1.5s
Type "y" Sleep 2.5s

# back to All, search
Type "h" Sleep 400ms
Type "h" Sleep 400ms
Type "h" Sleep 900ms
Type "/" Sleep 500ms
Type "redis" Sleep 1.2s
Enter Sleep 900ms
Type "d" Sleep 1.5s
Type "n" Sleep 1.5s

Type "q" Sleep 1s
