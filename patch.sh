wget https://www.nsnam.org/releases/ns-allinone-3.44.tar.bz2

tar -xjf ns-allinone-3.44.tar.bz2

mv ./ns-allinone-3.44/ns-3.44 ./ns3feat

cd ./ns3feat && \
rm -rf .git && \
git init && \
git add -A && \
git commit -m "Initial commit" && \
cd ..

git clone git@github.com:161043261/ns3.44.git

cp ./ns3.44/scratch/*.cc ./ns3feat/scratch/

cp ./ns3.44/src/internet/model/tcp-socket-base.h \
   ./ns3feat/src/internet/model/tcp-socket-base.h

cp ./ns3.44/src/internet/model/tcp-socket-base.cc \
   ./ns3feat/src/internet/model/tcp-socket-base.cc

cp ./ns3.44/src/internet/model/tcp-bbr.h \
   ./ns3feat/src/internet/model/tcp-bbr.h

cp ./ns3.44/src/internet/model/tcp-bbr.cc \
   ./ns3feat/src/internet/model/tcp-bbr.cc

cp ./ns3.44/.gitignore ./ns3feat/.gitignore

cd ./ns3feat && \
git add -A && \
git commit -m "feat: Introduce new features" && \
./ns3 configure && \
./ns3 build
