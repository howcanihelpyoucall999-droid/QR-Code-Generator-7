// Zaglogic.js — UI-only demo logic (non-invasive)
(function(){
  const textarea = document.getElementById('chatMessageInput');
  const sendBtn = document.getElementById('sendChatMessageBtn');
  const messages = document.getElementById('chat-messages');
  const placeholder = document.getElementById('emptyChatPlaceholder');
  const inputArea = document.querySelector('.chat-input-area');

  // Create placeholder if not present (defensive)
  function updatePlaceholderVisibility(){
    const hasBubbles = messages.querySelector('.bubble') !== null;
    const inputEmpty = !textarea.value.trim();
    if(!hasBubbles && inputEmpty){
      placeholder.classList.remove('hidden');
      placeholder.setAttribute('aria-hidden','false');
    } else {
      placeholder.classList.add('hidden');
      placeholder.setAttribute('aria-hidden','true');
    }
  }

  // Observe message container for added/removed bubbles
  const mo = new MutationObserver((list)=>{
    for(const m of list){
      if(m.type === 'childList'){
        // show/hide based on existing bubbles and input
        updatePlaceholderVisibility();
      }
    }
  });
  mo.observe(messages, { childList: true, subtree: false });

  // Hide placeholder while typing
  textarea.addEventListener('input', ()=> {
    // If user typed anything, hide placeholder with animation
    if(textarea.value.trim()){
      placeholder.classList.add('hidden');
    } else {
      updatePlaceholderVisibility();
    }
  });

  // send message (with send animation)
  function doSend(){
    const text = textarea.value.replace(/\n+$/,'');
    if(!text.trim()) return;
    // send animation
    inputArea.classList.add('sending');
    // short delay to show send animation
    setTimeout(()=>{
      inputArea.classList.remove('sending');
      // create bubble
      const b = document.createElement('div');
      b.className = 'bubble me';
      b.setAttribute('role','article');
      b.innerHTML = escapeHtml(text) + '<span class="msg-meta" aria-hidden="true"> • just now</span>';
      messages.appendChild(b);
      // force reflow then add enter class
      void b.offsetWidth;
      b.classList.add('enter');
      // clear textarea
      textarea.value = '';
      textarea.focus();
      // scroll
      b.scrollIntoView({behavior:'smooth',block:'end'});
      updatePlaceholderVisibility();
      // optionally simulate bot reply
      setTimeout(()=>simulateReply(), 700 + Math.random()*400);
    }, 220);
  }

  // simulate a bot reply (non-essential)
  function simulateReply(){
    const replies = [
      "Nice — tell me more.",
      "Okay, I got that.",
      "Here's a quick idea: try breaking it into smaller steps.",
      "Can you clarify a little?",
      "Sounds good — I'm on it!"
    ];
    const msg = replies[Math.floor(Math.random()*replies.length)];
    const b = document.createElement('div');
    b.className = 'bubble other';
    b.setAttribute('role','article');
    b.innerHTML = escapeHtml(msg) + '<span class="msg-meta" aria-hidden="true"> • ZagAI</span>';
    messages.appendChild(b);
    // add enter
    void b.offsetWidth;
    b.classList.add('enter');
    b.scrollIntoView({behavior:'smooth',block:'end'});
    updatePlaceholderVisibility();
  }

  // Send on button
  sendBtn.addEventListener('click', (e)=>{
    e.preventDefault();
    doSend();
  });

  // Enter to send, Shift+Enter newline
  textarea.addEventListener('keydown', (e)=>{
    if(e.key === 'Enter' && !e.shiftKey){
      e.preventDefault();
      doSend();
    }
  });

  // small helper
  function escapeHtml(str){
    return str.replace(/[&<>"']/g, (m)=>({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
  }

  // initial visibility on load
  window.addEventListener('load', ()=>{
    updatePlaceholderVisibility();
    // give focus to textarea for quick demo
    setTimeout(()=>{ textarea.focus(); }, 50);
  });

  // expose for debugging (non-invasive)
  window._ZagAI_UI = { updatePlaceholderVisibility };
})();
