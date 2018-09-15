# proot -- Termux の開発コミュニティによる proot の Debian noroot 環境向け修正版

## 概要

この git リポジトリに置かれている [proot][PROT] のソースコードは、 [termux の開発コミュニティ][TERM]による [proot][PROT] のソースコードを、 [Debian noroot 環境][DBNR]上で動作させるように修正したものです。

なお、この git リポジトリに置かれている [proot][PROT] のソースコードをビルドするには、[termux の開発コミュニティによる proot をビルドするための Ruby スクリプト][BLPR]に同梱されている ```build-proot.rb``` を使用して、ビルドを行って下さい。

## Termux の開発コミュニティによる proot のソースコードからの修正点

この git リポジトリに置かれている [proot][PROT] のソースコードは、オリジナルとなる [termux の開発コミュニティ][TERM]による [proot][PROT] のソースコードから、以下の不具合等が修正されています。

- Android NDK の toolchain の他、 PC 上のクロスコンパイル環境においてもビルドが可能となるように修正。
- VFAT 領域等、シンボリックリンクに対応していないファイルシステムの領域において、システムコール [link(2)][LINK] を実行した時に、リンク元のファイルが別名に変更されたままとなる問題を修正。
- [proot][PROT] コマンドにおいて、オプション ```--link2symlink``` が指定され、かつ、環境変数 ```PROOT_L2S_DIR``` が設定されない場合に、自動的にオプション ```-H``` が指定されて [proot][PROT] コマンドが起動されるように修正。
    - また、オプション ```-H``` が設定された場合に、 [proot][PROT] によって不可視化されるファイル及びシンボリックリンクのプレフィックスを ".l2s." とするように修正。
    - [link(2)][LINK] を [symlink(2)][SLNK] によってエミュレートする機能を使用時に [proot][PROT] の内部で作成される ".l2s." をプレフィックスとするファイル及びシンボリックリンクが外部から直接読み書きが出来るためにこれらのファイルを削除すると、ハードリンクが機能しなくなるために行われる修正です。
- ソースコード ```src/syscall/socket.c``` において、 obsolete である glibc の標準ライブラリ関数 ```mktemp(3)``` に代えて独自の実装による ```mktemp(3)``` 関数である ```proot_mktemp``` 関数を使用するように修正。
- ソースコード ```src/cli/proot.c, src/syscall/rlimit.c, src/tracee/mem.c``` 等において、コンパイル時に警告を出力する問題を修正。
- proot の一時ファイルを置くためのディレクトリのパス名の設定について、環境変数 ```PROOT_TMP_DIR``` の他に、環境変数 ```PROOT_TMPDIR``` を参照するように修正。

## 配布条件

この git リポジトリに置かれている [proot][PROT] のソースコードは、 [termux の開発コミュニティ][TERM]による [proot][PROT] のソースコードを、 [Z.OOL. (mailto:zool@zool.jpn.org)][ZOOL] によって、 [Debian noroot 環境][DBNR]及び PC 上のクロスコンパイル環境においてビルド及び動作するよう修正したものです。

従って、この [proot][PROT] のソースコードは、この git リポジトリに同梱されている [COPYING][COPY] の冒頭に記述されている STMicroelectronics 社の [proot][PROT] の開発者の各氏と [termux の開発コミュニティの各氏][TERM]及び [Z.OOL. (mailto:zool@zool.jpn.org)][ZOOL] が著作権を有し、オリジナルとなる [proot][PROT] のソースコードと同様に、 [GNU public license version 2][GPL2] に従って配布されるものとします。

## 追記

以下に、オリジナルの [proot][PROT] のソースコードの [README.md][READ] の原文を示します。

----
proot
=====
[![Travis build status](https://travis-ci.org/termux/proot.svg?branch=master)](https://travis-ci.org/termux/proot)

This is a copy of [the PRoot project](https://github.com/proot-me/PRoot/) with patches applied to work better under [Termux](https://termux.com).

<!-- 外部リンク一覧 -->

[TERM]:https://termux.com/
[DBNR]:https://play.google.com/store/apps/details?id=com.cuntubuntu&hl=ja
[BLPR]:https://github.com/z80oolong/proot-termux-build
[LINK]:http://man7.org/linux/man-pages/man2/link.2.html
[SLNK]:http://man7.org/linux/man-pages/man2/symlink.2.html
[PROT]:https://github.com/termux/proot
[TMRP]:https://github.com/termux
[ZOOL]:http://zool.jpn.org/
[COPY]:https://github.com/z80oolong/proot/blob/master/COPYING
[GPL2]:https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
[READ]:https://github.com/termux/proot/blob/master/README.md
