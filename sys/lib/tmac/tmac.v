'\"		Copyright (c) 1984 AT&T
'\"		  All Rights Reserved
'\"#ident	"@(#)macros:vmca.src	1.17"
.if n .ds Tm \uTM\d
.if t .ds Tm \v'-0.5m'\s-8TM\s+8\v'0.5m'
.
.de VS\"	foil-# foil-id date: start 7" wide × 7" high foil
.)j
.nr )K 0i
.nr )L 0i
.nr )U 8i
.nr )X 18
.nr )H 0
.nr )W 0
.nr )O 0i
.nr )M 0
.if !\\n(.$ .)V
.if \\n(.$ .if !\\n(.$-1 .)V "\\$1"
.if \\n(.$-1 .if !\\n(.$-2 .)V "\\$1" "\\$2"
.if \\n(.$-2 .)V "\\$1" "\\$2" "\\$3"
..
.de Vh\"	.VS but 5" wide × 7" high
.)j
.nr )K 0.9i
.nr )L 1i
.nr )U 8i
.nr )X 14
.nr )H 1
.nr )W 0
.nr )O 0.2i
.nr )M 0
.if !\\n(.$ .)V
.if \\n(.$ .if !\\n(.$-1 .)V "\\$1"
.if \\n(.$-1 .if !\\n(.$-2 .)V "\\$1" "\\$2"
.if \\n(.$-2 .)V "\\$1" "\\$2" "\\$3"
..
.de Sh\"	.VS but 5" wide × 7" high
.)j
.nr )K 1.1i
.nr )L 1i
.nr )U 8i
.nr )X 14
.nr )H 1
.nr )W 0
.nr )O 0.2i
.nr )M 1
.if !\\n(.$ .)V
.if \\n(.$ .if !\\n(.$-1 .)V "\\$1"
.if \\n(.$-1 .if !\\n(.$-2 .)V "\\$1" "\\$2"
.if \\n(.$-2 .)V "\\$1" "\\$2" "\\$3"
..
.de Vw\"	.VS but 7" wide × 5" high
.)j
.nr )K 0i
.nr )L 0i
.nr )U 6i
.nr )X 14
.nr )H 0
.nr )W 1
.nr )O 0.2i
.nr )M 0
.if !\\n(.$ .)V
.if \\n(.$ .if !\\n(.$-1 .)V "\\$1"
.if \\n(.$-1 .if !\\n(.$-2 .)V "\\$1" "\\$2"
.if \\n(.$-2 .)V "\\$1" "\\$2" "\\$3"
..
.de Sw\"	.VS but 7" wide × 5" high
.)j
.nr )K 0i
.nr )L 0i
.nr )U 6i
.nr )X 14
.nr )H 0
.nr )W 1
.nr )O 0.2i
.nr )M 1
.if !\\n(.$ .)V
.if \\n(.$ .if !\\n(.$-1 .)V "\\$1"
.if \\n(.$-1 .if !\\n(.$-2 .)V "\\$1" "\\$2"
.if \\n(.$-2 .)V "\\$1" "\\$2" "\\$3"
..
.de VH\"	.VS but 7" wide × 9" high
.)j
.nr )K 0i
.nr )L 0i
.nr )U 10i
.nr )X 18
.nr )H 1
.nr )W 0
.nr )O 0.5i
.nr )M 0
.if !\\n(.$ .)V
.if \\n(.$ .if !\\n(.$-1 .)V "\\$1"
.if \\n(.$-1 .if !\\n(.$-2 .)V "\\$1" "\\$2"
.if \\n(.$-2 .)V "\\$1" "\\$2" "\\$3"
..
.de SH\"	.VS but 7" wide × 9" high
.)j
.nr )K 0.5i
.nr )L 0i
.nr )U 10i
.nr )X 18
.nr )H 1
.nr )W 0
.nr )O 0.5i
.nr )M 1
.if !\\n(.$ .)V
.if \\n(.$ .if !\\n(.$-1 .)V "\\$1"
.if \\n(.$-1 .if !\\n(.$-2 .)V "\\$1" "\\$2"
.if \\n(.$-2 .)V "\\$1" "\\$2" "\\$3"
..
.de VW\"	.VS but 7" wide × 5.4" high
.)j
.nr )K 0i
.nr )L 0i
.nr )U 6.44i
.nr )X 14
.nr )H 0
.nr )W 1
.nr )O 0.4i
.nr )M 0
.if !\\n(.$ .)V
.if \\n(.$ .if !\\n(.$-1 .)V "\\$1"
.if \\n(.$-1 .if !\\n(.$-2 .)V "\\$1" "\\$2"
.if \\n(.$-2 .)V "\\$1" "\\$2" "\\$3"
..
.de SW\"	.VS but 7" wide × 5.4" high
.)j
.nr )K 0i
.nr )L 0i
.nr )U 6.44i
.nr )X 14
.nr )H 0
.nr )W 1
.nr )O 0.4i
.nr )M 1
.if !\\n(.$ .)V
.if \\n(.$ .if !\\n(.$-1 .)V "\\$1"
.if \\n(.$-1 .if !\\n(.$-2 .)V "\\$1" "\\$2"
.if \\n(.$-2 .)V "\\$1" "\\$2" "\\$3"
..
.
.de )V
.fc
.wh 0i
.if \\nX .wh -0.5i
.nr )o 0u
.if \\n()i .nr )o 10p
.nr )U +\\n()ou
.pl \\n()Uu+1.5i
.if \\nX .if \\n(.pu-\\n()Gu .pl \\n()Gu
.na
.fi
.nh
.lg 0
.ta 0.5i 1i 1.5i 2i 2.5i 3i 3.5i 4i 4.5i 5i 5.5i 6i
.ce 0
.in 0i
.ll 7.54i
.po 0i
.lt 7.68i
.if \\n()i .)t "'\(da cut \(da''\(da cut \(da'" 1
.nr )i 0
.)t "'\l'0.38i'''\l'0.38i''" 1
.sp 0.5v
.po 0.23i
.lt 7.06i
.if \\n(.$-1 .ds )N "\\$2
.if \\n(.$-2 .ds )Y "\\$3
.tl \\*()Y
.tl \\*()N
.if \\n(.$ .tl \\*()F \\$1
.if !\\n(.$ .tl \\*()F %
.po
.sp |0.68i+\\n()ou
.po 0.26i+\\n()Lu+\\n()Ou
.lt 7.03i-\\n()Lu-\\n()Lu-\\n()Ou-\\n()Ou
.if \\n()H .)t "'|''|'"
.po
.sp |1i+\\n()Ou+\\n()ou
.if !\\n(.A .)e
.po 0.1i
.lt 7.4i
.if \\n()W .)t "'_''_'"
.po
.sp |1i+\\n()ou
.if !\\n(.A .)e
.po 0.23i+\\n()Lu
.lt 7.06i-\\n()Lu-\\n()Lu
.tl ++
.sp -3p
.if \\n()M .if \\n()W .sp \\n()Ou
.po
.nr )J \\n()Uu
.if \\n()M .if \\n()W .nr )J \\n()Uu-\\n()Ou
.nr )E \\n()Ju
.wh \\n()Eu )Z
.S \\n()X 6i-\\n()Ku-\\n()Ku
.I "" A no-space
.sp .5v
.nr )n \\n(nlu
..
.de )Z
.wh \\n()Eu
.nr )w 0
.ev 1
.)g
'sp |\\n()Uu
.if !\\n(.A .)e
.po 0.23i+\\n()Lu
.lt 7.06i-\\n()Lu-\\n()Lu
.tl ++
.po
'sp |\\n()Uu-\\n()Ou
.if !\\n(.A .)e
.po 0.1i
.lt 7.4i
.if \\n()W .)t "'_''_'"
.po
'sp |\\n()Uu+0.32i
.po 0.26i+\\n()Lu+\\n()Ou
.lt 7.03i-\\n()Lu-\\n()Lu-\\n()Ou-\\n()Ou
.if \\n()H .)t "'|''|'"
.po
.if !\\n()H 'sp 1v
'sp 1v
.ev
.if \\nX .pl \\n()Gu
.if \\nX .wh -0.5i )m
.wh 0i )P
..
.de )m
.bp
..
.de )z
.pl \\n(.pu+2i
.br
.if \\n()i \{.ps 10
.lt 6i
.tl *** No input or no ``foil start'' macro in input. \}
.if !\\n()i \{.nr )x \\n(.vu+\\n()Eu-\\n(nlu/\\n(.vu
.nr )v \\n()w
.sp |\\n(.pu-2.8i
.if \\nX .wh -0.5i
.if \\nX .pl \\n(.pu+2i
\&
.br
.)g
.po 0i
.ll 6i
.ce 0
.in 0i
.if \\n(.A .sp -1.1v
.if \\n(.A .sp 0.1v
.if \\n(.A .if \\n()v ==> Approximately \\n()x blank line(s) \
left to bottom of previous foil.
.if \\n(.A .if !\\n()v ==> *** Previous foil full; \
check for overflow.
.sp |\\n(.pu-2i
.lt 7.68i
.)t "'\l'0.38i'''\l'0.38i''" 1
.)t "'\(ua cut \(ua''\(ua cut \(ua'" 1 \}
..
.de )t
.if !\\n(.A .if \\n(.$-1 .)e
.if !\\n(.A .tl \\$1
..
.de )e
.po 0i
.lt 1i
.tl '\ '''
'sp -1v
.po
.lt
..
.de )P
.pl 2i
.if \\nX .pl \\n()Gu
..
.de )j
.br
.if \\n()i .if \\n(nl \{.ps 10
.lt 6i
.tl *** Text before ``foil start'' macro in input. \}
.if !\\n()i \{.nr )x \\n(.vu+\\n()Eu-\\n(nlu/\\n(.vu
.nr )v \\n()w
.sp |\\n(.pu-0.8i
\&
.br\}
.)g
.if !\\n()i \{.po 0i
.ll 6i
.ce 0
.in 0i
.if \\n(.A .sp -1.1v
.if \\n(.A .sp 0.1v
.if \\n(.A .if \\n()v ==> Approximately \\n()x blank line(s) \
left to bottom of previous foil.
.if \\n(.A .if !\\n()v ==> *** Previous foil full; \
check for overflow.
.br
.ll
.po\}
.if \\n(nl .bp
.nr )w 1
..
.de )g
.ft \\*()f
.cs \\*()f
.ps 8
.vs 10p
.ss 16
..
.
.de T\"		string: title
.br
.if \\n(nlu-\\n()nu .sp .5v
.nr ]a \\n(.s
.ps
.nr ]b \\n(.s
.ps
.nr )u \\n(.iu
.in 0i
.ps +4
.ce
\&\\$1
.in \\n()uu
.ps \\n(]b
.ps \\n(]a
.sp .5v
.nr )n \\n(nlu
..
.de S\"		ps line-length: set point size & line length
.if !\w\\$1 .ps
.if \w\\$1 .if !\\$1+1 .ps \\$1
.if \w\\$1 .if \\$1 .nr )y \\n(.s
.if \w\\$1 .if \\$1 .nr )y \\$1
.if \w\\$1 .if \\$1-99 .nr )y \\n()X
.if \w\\$1 .if \\$1 .ps \\n()y
.vs \\n(.sp*5u/4u
.ss 16
.nr )a .8i+\\n()Ku
.nr ]c \\n(.s
.ps
.nr ]d \\n(.s
.ps
.nr )A \\n(.s*\\n()Q/\\n(]X
.ps \\n()A
.nr )b \w\\*()B\ u
.ps
.nr )A \\n(.s*\\n()R/\\n(]X
.ps \\n()A
.nr )c \\n()bu+\w\\*()C\ u
.ps
.nr )A \\n(.s*\\n()S/\\n(]X
.ps \\n()A
.nr )d \\n()cu+\w\\*()D\ u
.ps \\n(]d
.ps \\n(]c
.if \\n(.$-1 .nr )u \\$2
.if \\n(.$-1 .if !\\n()uu-7u .nr )p \\$2i
.if \\n(.$-1 .if \\n()uu-7u .nr )p \\$2
.nr )q \\n()pu-0i
.nr )r \\n()qu-0i
.nr )s \\n()ru-0i
.nr )T \\n(.sp*5u/4u
.nr )E \\n()Ju-\\n()Tu
.ch )Z \\n()Eu
..
.de I\"		in a a-arg: set text indent
.if !\w\\$1u .nr )k 0i
.if \w\\$1u .if !\\$1 .nr )u 0i-\\$1
.if \w\\$1u .if \\$1 .nr )u \\$1
.if \w\\$1u .if !\\n()uu-7u .nr )k \\$1i
.if \w\\$1u .if \\n()uu-7u .nr )k \\$1
.if \\n(.$-1 .A \\$3
..
.de A\"		nospace: 1st indentation level
.br
.if !\\n(.$ .if \\n(nlu-\\n()nu .sp \\*(]Au
.nr )n \\n(nlu
.po \\n()au
.in 0u+\\n()ku
.ll \\n()pu
.lt \\n()pu
..
.de B\"		mark ±ps: 2nd indentation level
.br
.if \\n(nlu-\\n()nu .sp \\*(]Bu
.nr )n \\n(nlu
.in \\n()bu+\\n()ku
.ll \\n()qu
.lt \\n()qu
.nr )l 0
.nr ]x \\n()bu
.if !\\n(.$ .)I \\*()B \\n()Q
.if \\n(.$ .if !\\n(.$-1 .)I "\\$1" 0
.if \\n(.$-1 .if \\$2-99 .)I "\\$1\ \|" \\n()Q
.if \\n(.$-1 .if !\\$2-99 .nr )l 1
.if \\n(.$-1 .if !\\$2-99 .)I "\\$1" "\\$2"
..
.de C\"		mark ±ps: 3rd indentation level
.br
.if \\n(nlu-\\n()nu .sp \\*(]Cu
.nr )n \\n(nlu
.in \\n()cu+\\n()ku
.ll \\n()ru
.lt \\n()ru
.nr )l 0
.nr ]x \\n()cu-\\n()bu
.if !\\n(.$ .)I \\*()C \\n()R
.if \\n(.$ .if !\\n(.$-1 .)I "\\$1" 0
.if \\n(.$-1 .if \\$2-99 .)I "\\$1\ \|" \\n()R
.if \\n(.$-1 .if !\\$2-99 .nr )l 1
.if \\n(.$-1 .if !\\$2-99 .)I "\\$1" "\\$2"
..
.de D\"		mark ±ps: 4th indentation level
.br
.if \\n(nlu-\\n()nu .sp \\*(]Du
.nr )n \\n(nlu
.in \\n()du+\\n()ku
.ll \\n()su
.lt \\n()su
.nr )l 0
.nr ]x \\n()du-\\n()cu
.if !\\n(.$ .)I \\*()D \\n()S
.if \\n(.$ .if !\\n(.$-1 .)I "\\$1" 0
.if \\n(.$-1 .if \\$2-99 .)I "\\$1\ \|" \\n()S
.if \\n(.$-1 .if !\\$2-99 .nr )l 1
.if \\n(.$-1 .if !\\$2-99 .)I "\\$1" "\\$2"
..
.de )I
.nr ]a \\n(.s
.ps
.nr ]b \\n(.s
.ps
.if !\\n()l .if !\\$2 .nr )A \\n(.s
.if !\\n()l .if \\$2 .nr )A \\n(.s*\\$2/\\n(]X
.if \\n()l .nr )A \\n(.s+\\$2
.ps \\n()A
.ti -\w\\$1\ u
\&\\$1\ \&\c
.ps \\n(]b
.ps \\n(]a
..
.de U\"		string suffix: underline string
.if !\w\\$1u-.46m \&\\$1\v'.55m'\l'|0\(hy'\v'-.55m'\\$2
.if \w\\$1u-.46m \&\\$1\v'.09m'\l'|0\(ul'\v'-.09m'\\$2
..
.de DV\"	a b c d: set vertical spacing for indent levels
.if \w\\$1 .ds ]A \\$1
.if \w\\$2 .ds ]B \\$2
.if \w\\$3 .ds ]C \\$3
.if \w\\$4 .ds ]D \\$4
..
.de DF\"	num font ...: define font positions
.if \\n(.$-1 .ds )f "\\$2
.if \\n(.$-1 .fp \\$1 \\$2
.if \\n(.$-1 .ft \\$2
.if \\n(.$-3 .fp \\$3 \\$4
.if \\n(.$-5 .fp \\$5 \\$6
.if \\n(.$-7 .fp \\$7 \\$8
..
.
.de SP
.sp \\$1
..
.de BR
.br
..
.de TA
'ta \\$1 \\$2 \\$3 \\$4 \\$5 \\$6 \\$7 \\$8 \\$9
..
.de CE
.ce \\$1
..
.de TI
.ti \\$1
..
.de FI
.fi
..
.de NF
.nf
..
.de AD
'ad \\$1
..
.de NA
'na
..
.de HY
'hy \\$1
..
.de NH
'nh
..
.de SO
'so \\$1
..
.de NX
'nx \\$1
..
.
.ds )F FOIL
.if \n(.A .ds )F FOIL
.ds )N Bell\ Labs
.ds )Y \n(mo/\n(dy/\n(yr
.ds )B \(bu\ \|
.ds )C \(em\ \|
.ds )D \(bu\ \|
.ds ]A .5v
.ds ]B .5v
.ds ]C .5v
.ds ]D 0v
.nr )G 11i-7.5p
.nr )Q 5
.nr )R 5
.nr )S 3
.nr ]X 6
.nr )i 1
.nr )w 0
.em )z
.DF 1 H
