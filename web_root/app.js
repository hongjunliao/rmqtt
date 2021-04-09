const {h, render, Component} = preact;
const html = htm.bind(h);

class Login extends Component {
  state = {user: '', pass: ''};
  onclick = (app) => {
    const authhdr = 'Basic ' + btoa(this.state.user + ':' + this.state.pass);
    const headers = {Authorization: authhdr};
    return axios.get('/api/login', {headers})
        .then(res => app.setUser(res.data.token))
        .catch(err => alert('Login failed'));
  };
  onpassinput = (ev) => this.setState({pass: ev.target.value});
  onuserinput = (ev) => this.setState({user: ev.target.value});
  render({app}, {user, pass, signup}) {
    return html`
      <div class='mx-auto bg-light rounded border my-5' style='max-width: 480px;'>
        <div class='form p-5 rounded form-sm'>
          <h4 class="text-muted mb-4">Welcome to rmqtt </h4>
          <input type='username' placeholder='rmqtt UserName' class='my-2 form-control'
            oninput=${this.onuserinput} value=${user} />
          <input type="password" placeholder="rmqtt Password" class="my-2 form-control"
            oninput=${this.onpassinput} value=${pass}
            onchange=${ev => this.onclick(app)} />
          <div class="mb-4">
            <button class="btn btn-info btn-block" 
    disabled=${!user || !pass} onclick="${ev => this.onclick(app)}"
            > Login </button>
          </div>
          <div class="text-muted small">
          see /etc/rmqtt.conf
          </div>
        </div>
      </div>
    `
  }
};

class Dropdown extends Component {
  state = {show: false};
  render(props, state) {
    const onclick = x => this.setState({show: x});
    const show = state.show ? 'show' : '';
    return html`
      <div class="dropdown autoexpand ${props.cls}">
        <div type="buttonx" onclick=${() => onclick(!state.show)}
          class="dropdown-toggle my-0 ${props.bcls}"
          data-toggle="dropdown" aria-haspopup="true" aria-expanded="false">
          ${props.title}
        </div>
        <div onclick=${() => onclick(false)} style=${props.dstyle}
          class="dropdown-menu ${props.dcls} ${show}">
          ${props.children}
        </div>
      </div>`;
  }
};

const NavLink = ({href, title, url}) => html`<a class="nav-item nav-link
    ${url == href ? 'active' : ''}"
    target=${href.match(/^http/) ? '_blank' : ''}
    href="${href.match(/^http/) ? '' : '#'}${href}">${title}</a>`;

class Header extends Component {
  state = {expanded: false};
  ontoggle = () => this.setState({expanded: !this.state.expanded});
  render(props, state) {
    const u = props.app.state.user || {};
    return html`
      <nav class="navbar navbar-expand-md navbar-dark bg-dark">
        <a class="navbar-brand" href="#">
          <img src="images/logo.png" width="26" height="26" alt="" class="mr-2" />
          rmqtt
        </a>
          <button class="navbar-toggler" type="button" data-toggle="collapse" data-target="#navbarNav" aria-controls="navbarNav" aria-expanded="false" aria-label="Toggle navigation" 
            onclick=${() => this.ontoggle()} >
          <span class="navbar-toggler-icon"></span>
        </button>
        <div class="navbar-collapse ${state.expanded ? '' : 'collapse'}"
            id="navbarNav">
          <div class="nav navbar-nav mr-auto">
            <${NavLink} href="/" title="Docs" url=${props.url} />
          </div>
        </div>
        <form class="form-inline">
          <${Dropdown} title="${u.user}" cls="mr-2" 
          bcls="btn btn-sm btn-outline-light pointer" dcls="m-0 dropdown-menu-right">
            <div onclick=${() => props.app.setUser('')}
              class="dropdown-item small pointer text-center">logout</div>
          </${Dropdown}>
          <img src="images/user.png" class="rounded-circle nav-item mr-2" width="30" />
        </form>
      </nav>
    `
  }
};

//{"topic":"a/b/c","payload":"Hello World", "encoding":"base64","qos":1,"retain":false,"clientid":"example"}
class PubForm extends Component {
  render(props, state) {
    const onclick = ev => axios.post(
        '/api/pub', {topic:state.value1,payload: state.value2})
        .catch(err => alert('Publish failed:\n' + err));;
    // alert(JSON.stringify(state));
    return html`<div id="form">
      <table>
        <tr><td>topic:</td><td><input class="form-control form-control-sm"
          value=${state.value1}
          onInput=${linkState(this, 'value1')} /></td></tr>
        <tr><td>message:</td><td><input class="form-control form-control-sm"
            value=${state.value2}
            onInput=${linkState(this, 'value2')}/></td></tr>
        <tr><td></td>
          <td><button class="btn btn-primary btn-block"
          disabled=${!state.value1 || !state.value2} onClick=${onclick}>OK</button></td></tr>
      </table>
    </div>`
  }
};

class Dashboard extends Component {
  render(props, state) {
    return html`<div class="container-fluid">
      <div class="card-deck">
        <div class="card">
          <div class="card-header">Publish</div>
          <div class="card-body"><${PubForm} /></div>
        </div>
      </div>
    </div>`
  }
}

class App extends Component {
  state = {user: null, url: '/'};
  setUser(token) {
    const maxAge = token ? 86400 : 0;
    document.cookie = `access_token=${token};path=/;max-age=${maxAge}`;
    // this.setState({token});
    return axios.get('/api/login')
        .then(res => this.setState({user: res.data}))
        .catch(err => this.setState({user: {}}));
  }
  componentDidMount() {
    const getCookie = name => {
      var v = document.cookie.match('(^|;) ?' + name + '=([^;]*)(;|$)');
      return v ? v[2] : '';
    };
    this.setUser(getCookie('access_token'));  // Move from state1 to 2 or 3
  }
  render(props, state) {
    // We have three states:
    // 1. Beginning. We don't know if we have a valid auth token or not
    // 2. We sent an /api/login request, and failed to login
    // 3. We sent an /api/login request, and succeeded to login

    if (!state.user) return '';                          // State 1
    if (!state.user.user) return h(Login, {app: this});  // State 2
    return h(                                            // State 3
        'div', {}, h(Header, {url: state.url, app: this}),
        h(preactRouter.Router, {
          history: History.createHashHistory(),
          onChange: ev => this.setState({url: ev.url}),
        },
        h(Dashboard, {default: true, app: this})));
  }
};

window.onload = () => render(h(App), document.body);
