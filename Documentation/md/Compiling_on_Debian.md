
# Compiling on Debian

This guide describes how to compile and run the devel branch under a Debian based system. It was tested using a fresh Debian Testing machine on Amazon EC2. For completeness, the steps pertaining to AWS are also included here.

## Requirements

ArangoDB v3.7 requires at least g++ 9.2 as compiler. Older versions, especially g++ 7, do not work anymore.

## Launch the VM (Optional)

Login to your AWS account and launch an instance of Debian Testing. I used an ‘m3.xlarge’ since that has a bunch of cores, more than enough memory, optimized network and the instance store is on SSDs which can be switched to provisioned IOPs.

The Current AMI ID’s can be found in the Debian Wiki: [Debian Wiki](https://wiki.debian.org/Cloud/AmazonEC2Image/)

## Upgrade to the very latest version (Optional)

Once your EC2 instance is up, login ad admin and `sudo su` to become root.

First, we remove the backports and change the primary sources.list

\`\`\`
rm -rf /etc/apt/sources.list.d
echo "deb     http://http.debian.net/debian testing main contrib"  > /etc/apt/sources.list
echo "deb-src http://http.debian.net/debian testing main contrib" >> /etc/apt/sources.list
\`\`\`

Update and upgrade the system. Make sure you don’t have any broken/unconfigured packages. Sometimes you need to run safe/full upgrade more than once. When you are done, reboot.

\`\`\`
apt-get update
apt-get install aptitude
aptitude -y update
aptitude -y safe-upgrade
aptitude -y full-upgrade
reboot
\`\`\`

## Install build dependencies (Mandatory)

Before you can build ArangoDB, you need a few packages pre-installed on your system.

Login again and install them.

\`\`\`
sudo aptitude -y install git-core \
    build-essential \
    libssl-dev \
    libjemalloc-dev \
    cmake \
    python2.7 \
sudo aptitude -y install libldap2-dev # Enterprise Edition only
\`\`\`

... (shortened for brevity)
