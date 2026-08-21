```bash
780  cd ..
  781  ./ircserv 
  782  ss -tlnp
  783  ss -tlnp | grep :3000
  784  $(ss -tlnp | grep :3000 && echo "3000")
  785  echo $(ss -tlnp | grep :3000 && echo "3000")
  786  echo $(ss -tlnp | grep :3000 ; echo "3000")
  787  $(ss -tlnp | grep :3000 ; echo "3000")
  788  ss -tlnp $(ss -tlnp | grep :3000 ; echo "3000") pass
  789  clear
  790  nc --help
  791  man nc
  792  nc 3000
  793  nc :3000
  794  nc -vz 0.0.0.0 3000
  795  nc  0.0.0.0 3000
  796  clauude
  797  rm -rf ~/.cache
  798  pwd
  799  df -h
  800  clear
  801  cd sgoinfre
  802  ls
  803  cd hellish/
  804  cat .seclaud 
  805  claude --resume 6fb91697-76d2-45df-b836-99d6d3375471
  806  cd soing
  807  cd sgoinfre
  808  cd ~/sgoinfre
  809  ls
  810  cd ..
  811  cd
  812  cd Documents/ft_irc/
  813  ls
  814  code .
  815  ping 10.11.15.14
  816  ip neigh show 10.11.15.14
  817  ip neigh show 10.11.15.4
  818  ping 10.11.15.4
  819  ip neigh show 10.11.15.4
  820  ip route get 10.11.15.14
  821  ip route get 10.11.15.4
  822  man wall
  823  wall "hello"
  824  msg * /server:10.11.15.4 "fasjklfljñask"
  825  which msg
  826  ss -ltnp
  827  systemctl list-units --type=service --state=running
  828  systemctl --type=service --state=running
  829  systemctl status crc.service
  830  systemctl cat crc.service
  831  systemctl show crc.service -p ExecStart -p User -p Description
  832  sudo ss -ltnp 'sport = :1337'
  833  nc -vz 127.0.0.1 1337
  834  nc -vz 10.11.15.6 1337
  835  ss -lunp
  836  ip maddr show
  837  sudo tcpdump -ni enp4s0f0
  838  cat /etc/crc/config.yml
  839  ls -l /etc/crc/config.yml
  840  ls -la /etc/crc/
  841  fuser 1337/tcp
  842  lsof -iTCP:1337 -sTCP:LISTEN
  843  ps aux | grep -v grep | grep -E '1337|crc'
  844  ps -fp 1395
  845  cat /proc/1395/cmdline | tr '\0' ' '; echo
  846  ls -l /proc/1395/fd
  847  nc -v 10.11.15.6 1337
  848  curl -v http://127.0.0.1:1337/
  849  curl -v http://10.11.15.6:1337/
  850  strings /bin/crc | grep -Ei 'http|https|tcp|udp|grpc|listen|port|server|message|notify|broadcast'
  851  watch -n 1 'ss -tn'
  852  nc -v 127.0.0.1 1337
  853  curl -v http://127.0.0.1:1337/
  854  /bin/crc --help
  855  curl -i http://127.0.0.1:1337/
  856  curl -i -X OPTIONS http://127.0.0.1:1337/
  857  curl -I http://127.0.0.1:1337/
  858  curl -i http://127.0.0.1:1337/does-not-exist
  859  command -v crc
  860  type -a crc
  861  curl -i http://127.0.0.1:1337/health
  862  curl -i http://127.0.0.1:1337/version
  863  curl -i http://127.0.0.1:1337/api
  864  ls -l /bin/crc /usr/bin/crc 2>&1
  865  dpkg -S /bin/crc 2>&1
  866  dpkg -l | grep -i crc
  867  grep -R "crc.service" /var/lib/dpkg/info 2>/dev/null | head
  868  ss -ltnp | grep ':1337'



```