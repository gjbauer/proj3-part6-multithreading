#!/bin/sh

OS=$(uname -s)

case "$OS" in
    Linux)
	echo "Operating System: Linux"
		if [ -f /etc/os-release ]; then
			. /etc/os-release
			if [ "$ID" = "linuxmint" ]; then
				echo "Supported Linux distribution: Linux Mint!!"
				
				DEPENDENCIES="libfuse-dev libbsd-dev"
				echo "Updating repositories..."
				if ! sudo apt update -y > /dev/null 2>&1; then
					echo "Failed to update repositories!!"
					return 1
				fi
				echo "Checking dependencies..."
				for pkg in $DEPENDENCIES; do
					if ! dpkg -s "$pkg" > /dev/null 2>&1; then
						echo "Installing missing dependency: $pkg"
						if ! sudo apt install -y "$pkg" > /dev/null 2>&1; then
							echo "Failed to install package!!"
							return 1
						fi
					else
						echo "Dependency already installed: $pkg"
					fi
				done
			elif [ "$ID" = "debian" ]; then
				echo "Supported Linux distribution: Debian!!"
				
				DEPENDENCIES="libfuse-dev libbsd-dev"
				echo "Updating repositories..."
				if ! sudo apt update -y > /dev/null 2>&1; then
					echo "Failed to update repositories!!"
					return 1
				fi
				echo "Checking dependencies..."
				for pkg in $DEPENDENCIES; do
					if ! dpkg -s "$pkg" > /dev/null 2>&1; then
						echo "Installing missing dependency: $pkg"
						if ! sudo apt install -y "$pkg" > /dev/null 2>&1; then
							echo "Failed to install package!!"
							return 1
						fi
					else
						echo "Dependency already installed: $pkg"
					fi
				done
			elif [ "$ID" = "ubuntu" ]; then
				echo "Supported Linux distribution: Ubuntu!!"
				
				DEPENDENCIES="libfuse-dev libbsd-dev"
				echo "Updating repositories..."
				if ! sudo apt update -y > /dev/null 2>&1; then
					echo "Failed to update repositories!!"
					return 1
				fi
				echo "Checking dependencies..."
				for pkg in $DEPENDENCIES; do
					if ! dpkg -s "$pkg" > /dev/null 2>&1; then
						echo "Installing missing dependency: $pkg"
						if ! sudo apt install -y "$pkg" > /dev/null 2>&1; then
							echo "Failed to install package!!"
							return 1
						fi
					else
						echo "Dependency already installed: $pkg"
					fi
				done
			else
				echo "Unsupported Linux distribution! returning..."
				return 1
			fi
		else
			echo "Cannot find release information!! returning..."
			return 1
		fi
	;;
    Darwin)
	echo "Operating System: macOS"
	brew install macfuse
    sudo cpanm Proc::Background
	;;
    FreeBSD)
	echo "Operating System: FreeBSD"
	echo "Supported OS!"

		DEPENDENCIES="fusefs-libs"
		echo "Updating repositories..."
		if ! env IGNORE_OSVERSION=yes doas pkg update > /dev/null 2>&1 ; then
			echo "Failed to update repositories!!"
			return 1
		fi
		echo "Checking dependencies..."
		for pkg in $DEPENDENCIES; do
			if ! pkg info -e "$pkg" > /dev/null 2>&1; then
				echo "Installing missing dependency: $pkg"
				if ! doas pkg install -y "$pkg" > /dev/null 2>&1 ; then
					echo "Failed to install package!!"
					return 1
				fi
			else
				echo "Dependency already installed: $pkg"
			fi
		done
	;;
    CYGWIN*|MINGW32*|MSYS*)
	echo "Operating System: Windows (via Cygwin/MinGW/MSYS)"
	echo "Unsupported OS: returning!"
	return 1
	;;
    *)
	echo "Operating System: Unknown ($OS)"
	echo "Unsupported OS: returning!"
	return 1
	;;
esac

echo "Done instqlling dependencies!"
