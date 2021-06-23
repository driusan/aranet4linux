{ pkgs ? import <nixpkgs> {} }:
pkgs.stdenv.mkDerivation {
  buildInputs = with pkgs; [ gcc dbus pkgconfig ];
  src = ./.;
  name = "aranet4";
}
