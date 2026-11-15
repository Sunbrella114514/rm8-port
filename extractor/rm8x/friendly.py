"""Plain-Chinese explanations for the failures an ordinary user can run into (no disk space, no permission, no network ...).
The technical detail always stays in extract.log; the console gets a sentence the user can act on."""
import errno

from . import netdl
from .util import human

ISSUES_HINT = '如果按上面的办法仍然不行，请到本项目仓库的 Issues 页面反馈，并附上输出文件夹里的 extract.log（里面没有你的个人信息，只有文件路径和步骤记录）。'


def is_disk_full(e):
    return isinstance(e, OSError) and (e.errno == errno.ENOSPC or getattr(e, 'winerror', None) in (112, 39, 1450))


def os_error_message(e):
    """Message for an OSError that reached the top level."""
    where = getattr(e, 'filename', None)
    at = '（%s）' % where if where else ''
    if is_disk_full(e):
        return ('磁盘空间不够了%s。请清理磁盘，或者换一个空间大的磁盘（输出文件夹、--work-dir、--toolchain-dir 都可以换）后重新运行；'
                '已经完成的步骤下次会自动跳过。\n  提示：提取数据约需 1.2 GB；构建电脑版的工具再约 1.2 GB；构建安卓版的工具再约 4.5 GB（下载的压缩包可以在构建完成后删掉工具缓存里的 downloads 文件夹）。' % at)
    if isinstance(e, PermissionError) or getattr(e, 'winerror', None) in (5, 32, 33):
        return ('没有权限写入，或文件正被别的程序占用%s。\n  - 请换一个你有权限的位置做输出文件夹（例如“文档”或 D 盘下新建的文件夹，不要放在 C:\\Program Files 里）；\n'
                '  - 关掉占用它的程序（例如正在运行的移植版、资源管理器预览窗格），暂时关闭杀毒软件的实时扫描后重试。' % at)
    if getattr(e, 'winerror', None) in (3, 123, 206) or e.errno in (errno.ENOENT, errno.ENAMETOOLONG):
        return '找不到文件夹，或路径太长 / 含有不能用的字符%s。请换一个路径短一些的文件夹（例如 D:\\RM8）。' % at
    if getattr(e, 'winerror', None) in (21, 1117, 23, 1392) or e.errno == errno.EIO:
        return '读写磁盘时出错%s：磁盘可能没有准备好，或文件已损坏（也可能是移动硬盘 / U 盘断开了）。请检查磁盘后重新运行。' % at
    return '读写文件时出错%s：%s' % (at, e)


def network_failure(what, urls, detail=''):
    """A download failed: say whether the net is down or only a host is unreachable."""
    bad = netdl.unreachable_hosts(urls, timeout=8)
    hosts = [h for h, _ in bad]
    msg = netdl.offline_message(what, hosts)
    if detail:
        msg += '\n  （技术细节：%s）' % detail.strip().splitlines()[-1][:200]
    return msg


def space_message(path, need, free, what='这一步'):
    return '磁盘空间不够：%s约需 %s，%s 所在的磁盘只剩 %s。请清理空间，或者换一个磁盘后重新运行。' % (what, human(need), path, human(free))
