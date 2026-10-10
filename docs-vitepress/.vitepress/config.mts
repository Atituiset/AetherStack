import { defineConfig } from 'vitepress'

const base = '/AetherStack/'

// https://vitepress.dev/reference/site-config
export default defineConfig({
  lang: 'zh-CN',
  title: 'AetherStack 技术文档',
  titleTemplate: '从零构建的 5G NR 无线协议栈',
  description:
    'AetherStack 是一个教育/演示级 5G NR 无线协议栈实现，覆盖 PHY / MAC / RLC / PDCP / RRC / NAS 全七层，可运行、可观测、可调试。',

  base,
  lastUpdated: true,
  cleanUrls: true,
  srcExclude: ['README.md'],

  markdown: {
    // shiki 3.x 不再内置 cuda 语法, 用 cpp 高亮兜底
    languageAlias: { cuda: 'cpp' },
  },

  head: [
    ['link', { rel: 'icon', type: 'image/svg+xml', href: `${base}favicon.svg` }],
    ['meta', { name: 'theme-color', content: '#2e7d5b', media: '(prefers-color-scheme: light)' }],
    ['meta', { name: 'theme-color', content: '#18191c', media: '(prefers-color-scheme: dark)' }],
    ['meta', { property: 'og:title', content: 'AetherStack 技术文档' }],
    [
      'meta',
      {
        property: 'og:description',
        content:
          '从零构建的 5G NR 无线协议栈 MVP：PHY / MAC / RLC / PDCP / RRC / NAS 全七层，双端可运行、全链路可观测。',
      },
    ],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { name: 'twitter:card', content: 'summary' }],
    // 学习笔记悬浮组件（localStorage），VitePress SPA 路由切换由 theme/index.ts 通知其重挂载
    ['script', { src: `${base}notes.js` }],
  ],

  themeConfig: {
    // https://vitepress.dev/reference/default-theme-config
    logo: '/favicon.svg',

    nav: [
      { text: '首页', link: '/' },
      { text: '项目概览', link: '/overview/project' },
      { text: '协议栈参考', link: '/stack/reference' },
      { text: '演示系统', link: '/demo/demo' },
      { text: 'API 参考', link: '/api/api' },
    ],

    sidebar: [
      { text: '首页', link: '/intro' },
      { text: '项目概览', link: '/overview/project' },
      { text: '技术栈与架构', link: '/overview/architecture' },
      { text: '里程碑路线图', link: '/overview/roadmap' },
      {
        text: '协议栈参考',
        collapsed: false,
        link: '/stack/reference',
        items: [
          { text: '公共基础设施', link: '/stack/common' },
          { text: '节点编排层 UeNode/BsNode', link: '/stack/orchestration' },
        ],
      },
      {
        text: 'PHY 物理层',
        collapsed: false,
        link: '/phy/phy',
        items: [
          { text: 'QPSK 调制解调', link: '/phy/qpsk' },
          { text: 'OFDM 收发', link: '/phy/ofdm' },
          { text: 'PHY I/O 序列化', link: '/phy/phy_io' },
          { text: 'PHY 测试', link: '/phy/phy_tests' },
        ],
      },
      {
        text: 'MAC 层',
        collapsed: false,
        link: '/mac/mac',
        items: [
          { text: 'MAC PDU 编解码', link: '/mac/mac_pdu' },
          { text: 'RACH 随机接入', link: '/mac/rach' },
          { text: 'MAC 测试', link: '/mac/mac_tests' },
        ],
      },
      {
        text: 'RLC / PDCP 层',
        collapsed: false,
        link: '/rlc-pdcp/rlc_pdcp',
        items: [
          { text: 'RLC 透明模式', link: '/rlc-pdcp/rlc_tm' },
          { text: 'PDCP 透传实体', link: '/rlc-pdcp/pdcp' },
          { text: 'RLC/PDCP 测试', link: '/rlc-pdcp/rlc_pdcp_tests' },
        ],
      },
      {
        text: 'RRC 层',
        collapsed: false,
        link: '/rrc/rrc',
        items: [
          { text: 'RRC 消息格式', link: '/rrc/rrc_messages' },
          { text: 'RRC UE / BS 实体', link: '/rrc/rrc_entities' },
          { text: 'RRC 测试', link: '/rrc/rrc_tests' },
        ],
      },
      {
        text: 'NAS 层',
        collapsed: false,
        link: '/nas/nas',
        items: [
          { text: 'NAS 消息格式', link: '/nas/nas_messages' },
          { text: 'NAS UE / BS 实体', link: '/nas/nas_entities' },
          { text: 'NAS 测试', link: '/nas/nas_tests' },
        ],
      },
      { text: '核心网层 (M15)', link: '/cn/cn' },
      { text: '用户面', link: '/user-plane/app_layer' },
      {
        text: '集成测试',
        collapsed: false,
        link: '/integration/integration',
        items: [
          { text: '全链路垂直测试', link: '/integration/vertical' },
          { text: '完整附着流程', link: '/integration/full_attach' },
          { text: '用户面 Ping-Pong', link: '/integration/user_plane' },
        ],
      },
      {
        text: 'Web LMT',
        collapsed: false,
        link: '/lmt/lmt',
        items: [
          { text: 'React 组件', link: '/lmt/components' },
          { text: 'WebSocket Hook', link: '/lmt/websocket' },
          { text: '事件目录镜像', link: '/lmt/events_ts' },
        ],
      },
      {
        text: 'Python 工具',
        collapsed: false,
        link: '/tools/tools',
        items: [
          { text: '日志服务器', link: '/tools/log_server' },
          { text: '信道模拟器', link: '/tools/sim_channel' },
          { text: '验证工具集', link: '/tools/e2e_harness' },
          { text: 'PHY 参考模型', link: '/tools/phy_ref' },
          { text: '分析脚本', link: '/tools/scripts' },
        ],
      },
      { text: '演示系统', link: '/demo/demo' },
      {
        text: '构建与部署',
        collapsed: false,
        link: '/buildsys/build',
        items: [{ text: '构建与运行', link: '/buildsys/demo' }],
      },
      {
        text: '里程碑历程',
        collapsed: false,
        link: '/milestones/m0_m6',
        items: [
          { text: 'M6.5 → M8 历程', link: '/milestones/m7_m8' },
          { text: 'M9 → M15 历程', link: '/milestones/m9_m15' },
        ],
      },
      { text: '技能卡片体系', link: '/skills/skills' },
      { text: 'API 参考', link: '/api/api' },
    ],

    outline: {
      level: [2, 3],
      label: '本页大纲',
    },

    search: {
      provider: 'local',
      options: {
        translations: {
          button: { buttonText: '搜索文档', buttonAriaLabel: '搜索文档' },
          modal: {
            noResultsText: '无法找到相关结果',
            resetButtonTitle: '清除查询条件',
            footer: { selectText: '选择', navigateText: '切换', closeText: '关闭' },
          },
        },
      },
    },

    editLink: {
      pattern: 'https://github.com/Atituiset/AetherStack/edit/trial/s1-m2/docs-vitepress/:path',
      text: '在 GitHub 上编辑此页',
    },

    socialLinks: [{ icon: 'github', link: 'https://github.com/Atituiset/AetherStack' }],

    docFooter: { prev: '上一页', next: '下一页' },

    lastUpdated: {
      text: '最后更新于',
      formatOptions: { dateStyle: 'short', timeStyle: 'short' },
    },

    returnToTopLabel: '回到顶部',
    sidebarMenuLabel: '菜单',
    darkModeSwitchLabel: '主题',
    lightModeSwitchTitle: '切换到浅色模式',
    darkModeSwitchTitle: '切换到深色模式',
    skipToContentLabel: '跳转到内容',

    footer: {
      message: '从零构建的 5G NR 无线协议栈 MVP',
      copyright: 'Copyright © 2026 AetherStack Team',
    },
  },
})
