# Regenerate with: PATH="$PWD/build:$PATH" vhs docs/demo.tape
Output docs/demo.gif

Set Shell "bash"
Set FontSize 16
Set Width 1100
Set Height 640
Set Padding 16
Set Theme "Dracula"
Set TypingSpeed 35ms

Hide
Type "export PS1='$ ' && rm -rf /tmp/fdr-demo && mkdir /tmp/fdr-demo && cd /tmp/fdr-demo && clear"
Enter
Show

Type "fdr record flight --start-us 1700000000000000 --rate 200 --count 3000 --segment-kib 64"
Enter
Sleep 1s
Type "fdr info flight | head -9"
Enter
Sleep 2s
Type "fdr export flight --from +1s --to +1.01s"
Enter
Sleep 2s
Type "truncate -s -9 flight/fdr-000002.seg   # simulate a torn write"
Enter
Type "fdr recover flight --dry-run; echo exit=$?"
Enter
Sleep 2s
Type "fdr recover flight | head -3 && fdr record flight --count 10"
Enter
Sleep 3s
